"""
trace_viewer_gantt.py
----------------------
Visualizador de trace em tempo real para o RTOS proprio (baseado em MiROS),
inspirado no estilo "Gantt de estados" do Eclipse Trace Compass: cada tarefa
(e o SysTick/ISR) tem uma "raia" horizontal, e o tempo (ticks) avanca da
esquerda para a direita mostrando em qual estado a tarefa estava a cada
instante (RODANDO, PRONTA, BLOQUEADA/AGUARDANDO).

Protocolo (igual ao trace_uart.c / miros.cpp do firmware):
    pacote de 8 bytes:
        byte 0: MAGIC = 0xAB
        byte 1: event id (1..10)
        byte 2: task id (0=idle, 1=task1, 2=task2, 3=task3)
        byte 3: reservado (0)
        bytes 4..7: tick (uint32, little-endian)

Uso:
    pip install pygame
    python trace_viewer_gantt.py [host] [port]

Controles:
    ESPACO  -> pausa/retoma o auto-scroll (o log continua sendo recebido e
               gravado, so a tela congela para voce poder olhar com calma)
    + / -   -> aumenta/diminui a janela de tempo visivel (zoom)
    ESC     -> fecha
"""

import socket
import struct
import sys
import threading
import queue
import time
import os
import csv

import pygame

# --------------------------------------------------------------------------
# Configuracao / protocolo
# --------------------------------------------------------------------------

HOST = sys.argv[1] if len(sys.argv) > 1 else "localhost"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 4444

MAGIC = 0xAB
PACKET_SIZE = 8

EV_SWITCH       = 1
EV_YIELD        = 2
EV_DELAY        = 3
EV_SEM_WAIT     = 4
EV_SEM_BLOCK    = 5
EV_SEM_SIGNAL   = 6
EV_SEM_WAKE     = 7
EV_ISR_ENTER    = 8
EV_THREAD_READY = 9
EV_ISR_EXIT     = 10

EVENT_NAMES = {
    EV_SWITCH: "SWITCH", EV_YIELD: "YIELD", EV_DELAY: "DELAY",
    EV_SEM_WAIT: "SEM_WAIT", EV_SEM_BLOCK: "SEM_BLOCK",
    EV_SEM_SIGNAL: "SEM_SIGNAL", EV_SEM_WAKE: "SEM_WAKE",
    EV_ISR_ENTER: "ISR_ENTER", EV_THREAD_READY: "THREAD_READY",
    EV_ISR_EXIT: "ISR_EXIT",
}

TASK_NAMES = {0: "idle", 1: "task1", 2: "task2", 3: "task3"}

# --------------------------------------------------------------------------
# Estados possiveis de uma raia (lane)
# --------------------------------------------------------------------------

RUNNING = "RUNNING"
READY = "READY"
BLOCKED = "BLOCKED"     # esperando semaforo
WAITING = "WAITING"     # esperando timeout (OS_delay)
IDLE_ISR = "IDLE"       # ISR inativa
ACTIVE_ISR = "ACTIVE"   # ISR em execucao
UNKNOWN = "UNKNOWN"

STATE_COLORS = {
    RUNNING:    (46, 204, 113),   # verde
    READY:      (241, 196, 15),   # amarelo
    BLOCKED:    (231, 76, 60),    # vermelho
    WAITING:    (230, 126, 34),   # laranja
    ACTIVE_ISR: (52, 152, 219),   # azul
    IDLE_ISR:   (44, 44, 48),
    UNKNOWN:    (100, 100, 100),
}

# --------------------------------------------------------------------------
# Recebimento em thread separada (nao perde dados enquanto a tela desenha)
# --------------------------------------------------------------------------

class TraceReceiver(threading.Thread):
    def __init__(self, host, port, out_queue):
        super().__init__(daemon=True)
        self.host = host
        self.port = port
        self.q = out_queue
        self._stop = threading.Event()

    def stop(self):
        self._stop.set()

    def run(self):
        buf = b""
        while not self._stop.is_set():
            try:
                self.q.put(("status", f"Conectando em {self.host}:{self.port}..."))
                with socket.create_connection((self.host, self.port), timeout=5) as sock:
                    sock.settimeout(1.0)
                    self.q.put(("status", "Conectado. Aguardando eventos..."))
                    buf = b""
                    while not self._stop.is_set():
                        try:
                            chunk = sock.recv(4096)
                            if not chunk:
                                break
                            buf += chunk
                            while len(buf) >= PACKET_SIZE:
                                idx = buf.find(bytes([MAGIC]))
                                if idx < 0:
                                    buf = b""
                                    break
                                if idx > 0:
                                    buf = buf[idx:]
                                if len(buf) < PACKET_SIZE:
                                    break
                                pkt = buf[:PACKET_SIZE]
                                event = pkt[1]
                                task = pkt[2]
                                tick = struct.unpack_from("<I", pkt, 4)[0]
                                self.q.put(("event", {"event": event, "task": task, "tick": tick}))
                                buf = buf[PACKET_SIZE:]
                        except socket.timeout:
                            continue
                self.q.put(("status", "Conexao encerrada pelo host remoto. Tentando reconectar..."))
            except (ConnectionRefusedError, OSError) as e:
                self.q.put(("status", f"Falha ao conectar ({e}). Tentando de novo em 2s..."))
                time.sleep(2)
                continue
            time.sleep(1)


# --------------------------------------------------------------------------
# Modelo: converte eventos em segmentos de estado por raia (lane)
# --------------------------------------------------------------------------

class Lane:
    """Uma raia do Gantt: guarda o historico de segmentos (estado, inicio, fim)."""
    def __init__(self, name):
        self.name = name
        self.segments = []          # lista de [estado, tick_inicio, tick_fim_ou_None]
        self.current_state = UNKNOWN
        self.current_start = 0
        self.markers = []           # eventos instantaneos (SEM_WAIT/SEM_SIGNAL): (tick, label)

    def set_state(self, tick, new_state):
        if self.current_state != UNKNOWN:
            self.segments.append([self.current_state, self.current_start, tick])
        self.current_state = new_state
        self.current_start = tick

    def add_marker(self, tick, label):
        self.markers.append((tick, label))

    def trim_before(self, min_tick):
        """Descarta segmentos/marcadores totalmente fora da janela visivel,
        para nao deixar as listas crescerem para sempre."""
        self.segments = [s for s in self.segments if s[2] is None or s[2] >= min_tick]
        self.markers = [m for m in self.markers if m[0] >= min_tick]


class TraceModel:
    def __init__(self):
        self.lanes = {}
        self.lanes["ISR"] = Lane("SysTick (ISR)")
        for tid, name in TASK_NAMES.items():
            self.lanes[tid] = Lane(name)
        # estado inicial das tarefas: consideramos "pronta" ate o primeiro evento real
        for tid in TASK_NAMES:
            self.lanes[tid].current_state = READY
            self.lanes[tid].current_start = 0
        self.lanes["ISR"].current_state = IDLE_ISR
        self.lanes["ISR"].current_start = 0

        self.pending_block_reason = {}   # task_id -> WAITING ou BLOCKED (setado antes do proximo SWITCH)
        self.current_tick = 0
        self.total_events = 0

    def process(self, evt):
        event = evt["event"]
        task = evt["task"]
        tick = evt["tick"]
        self.current_tick = max(self.current_tick, tick)
        self.total_events += 1

        if event == EV_ISR_ENTER:
            self.lanes["ISR"].set_state(tick, ACTIVE_ISR)

        elif event == EV_ISR_EXIT:
            self.lanes["ISR"].set_state(tick, IDLE_ISR)

        elif event == EV_SWITCH:
            new_lane = self.lanes.get(task)
            # rebaixa quem estava RUNNING para READY/WAITING/BLOCKED
            for tid, lane in self.lanes.items():
                if tid == "ISR":
                    continue
                if lane.current_state == RUNNING and tid != task:
                    reason = self.pending_block_reason.pop(tid, READY)
                    lane.set_state(tick, reason)
            if new_lane is not None:
                self.pending_block_reason.pop(task, None)
                new_lane.set_state(tick, RUNNING)

        elif event == EV_THREAD_READY:
            lane = self.lanes.get(task)
            if lane is not None and lane.current_state != RUNNING:
                lane.set_state(tick, READY)

        elif event == EV_DELAY:
            # a tarefa que chamou delay vai ficar aguardando quando for
            # trocada de contexto (proximo SWITCH cuida disso)
            self.pending_block_reason[task] = WAITING

        elif event == EV_SEM_BLOCK:
            self.pending_block_reason[task] = BLOCKED

        elif event == EV_SEM_WAKE:
            lane = self.lanes.get(task)
            if lane is not None and lane.current_state != RUNNING:
                lane.set_state(tick, READY)
            self.pending_block_reason.pop(task, None)

        elif event in (EV_SEM_WAIT, EV_SEM_SIGNAL, EV_YIELD):
            lane = self.lanes.get(task)
            if lane is not None:
                lane.add_marker(tick, EVENT_NAMES[event])

    def trim(self, window_ticks):
        min_tick = max(0, self.current_tick - window_ticks * 3)
        for lane in self.lanes.values():
            lane.trim_before(min_tick)


# --------------------------------------------------------------------------
# Render (pygame)
# --------------------------------------------------------------------------

LANE_ORDER = ["ISR", 0, 1, 2, 3]

BG_COLOR = (24, 24, 27)
GRID_COLOR = (55, 55, 60)
TEXT_COLOR = (230, 230, 230)
DIM_TEXT_COLOR = (150, 150, 155)

MARGIN_LEFT = 150
MARGIN_TOP = 70
MARGIN_BOTTOM = 50
MARGIN_RIGHT = 30
LANE_HEIGHT = 78
LANE_GAP = 10
BAR_HEIGHT = 46


def draw_legend(screen, font, x, y):
    items = [
        (RUNNING, "Rodando"),
        (READY, "Pronta"),
        (BLOCKED, "Bloqueada (semaforo)"),
        (WAITING, "Aguardando (delay)"),
        (ACTIVE_ISR, "ISR ativa"),
    ]
    for i, (state, label) in enumerate(items):
        box_x = x + i * 190
        pygame.draw.rect(screen, STATE_COLORS[state], (box_x, y, 16, 16))
        txt = font.render(label, True, TEXT_COLOR)
        screen.blit(txt, (box_x + 22, y - 2))


def main():
    pygame.init()
    pygame.display.set_caption(f"Trace Viewer - MiROS RTOS ({HOST}:{PORT})")
    screen = pygame.display.set_mode((1150, 620), pygame.RESIZABLE)
    clock = pygame.time.Clock()
    font = pygame.font.SysFont("consolas", 16)
    font_small = pygame.font.SysFont("consolas", 13)
    font_title = pygame.font.SysFont("consolas", 20, bold=True)

    q = queue.Queue()
    receiver = TraceReceiver(HOST, PORT, q)
    receiver.start()

    model = TraceModel()
    status_text = "Iniciando..."
    paused = False
    window_ticks = 300

    # log em CSV, ao lado deste script, para usar na documentacao/README
    log_dir = os.path.dirname(os.path.abspath(__file__))
    log_path = os.path.join(log_dir, "trace_log.csv")
    log_file = open(log_path, "w", newline="")
    log_writer = csv.writer(log_file)
    log_writer.writerow(["tick", "event", "task"])

    running = True
    while running:
        for pyevt in pygame.event.get():
            if pyevt.type == pygame.QUIT:
                running = False
            elif pyevt.type == pygame.VIDEORESIZE:
                screen = pygame.display.set_mode((pyevt.w, pyevt.h), pygame.RESIZABLE)
            elif pyevt.type == pygame.KEYDOWN:
                if pyevt.key == pygame.K_ESCAPE:
                    running = False
                elif pyevt.key == pygame.K_SPACE:
                    paused = not paused
                elif pyevt.key in (pygame.K_PLUS, pygame.K_EQUALS, pygame.K_KP_PLUS):
                    window_ticks = max(20, window_ticks - 20)
                elif pyevt.key in (pygame.K_MINUS, pygame.K_KP_MINUS):
                    window_ticks = min(5000, window_ticks + 20)

        # drena a fila inteira a cada frame -> nunca perde eventos por causa do desenho
        drained = 0
        while True:
            try:
                kind, payload = q.get_nowait()
            except queue.Empty:
                break
            if kind == "status":
                status_text = payload
            elif kind == "event":
                model.process(payload)
                log_writer.writerow([payload["tick"], EVENT_NAMES.get(payload["event"], payload["event"]),
                                      TASK_NAMES.get(payload["task"], payload["task"])])
                drained += 1
        if drained:
            log_file.flush()
        model.trim(window_ticks)

        # ---------------- desenho ----------------
        screen.fill(BG_COLOR)
        w, h = screen.get_size()
        plot_x0 = MARGIN_LEFT
        plot_x1 = w - MARGIN_RIGHT
        plot_w = max(10, plot_x1 - plot_x0)

        title = font_title.render("Trace Viewer - MiROS RTOS (estilo Gantt de estados)", True, TEXT_COLOR)
        screen.blit(title, (20, 15))

        draw_legend(screen, font_small, MARGIN_LEFT, 42)

        end_tick = model.current_tick
        start_tick = max(0, end_tick - window_ticks)
        span = max(1, end_tick - start_tick)

        def tick_to_x(t):
            t = min(max(t, start_tick), end_tick)
            return plot_x0 + (t - start_tick) / span * plot_w

        y = MARGIN_TOP
        for key in LANE_ORDER:
            lane = model.lanes[key]
            label = font.render(lane.name, True, TEXT_COLOR)
            screen.blit(label, (10, y + LANE_HEIGHT // 2 - 8))

            bar_y = y + (LANE_HEIGHT - BAR_HEIGHT) // 2
            pygame.draw.rect(screen, (35, 35, 40), (plot_x0, bar_y, plot_w, BAR_HEIGHT))

            for state, seg_start, seg_end in lane.segments:
                if seg_end is not None and seg_end < start_tick:
                    continue
                x0 = tick_to_x(seg_start)
                x1 = tick_to_x(seg_end if seg_end is not None else end_tick)
                rect_w = max(1, x1 - x0)
                pygame.draw.rect(screen, STATE_COLORS.get(state, UNKNOWN), (x0, bar_y, rect_w, BAR_HEIGHT))

            # segmento ainda aberto (estado atual, vai ate "agora")
            x0 = tick_to_x(lane.current_start)
            x1 = tick_to_x(end_tick)
            pygame.draw.rect(screen, STATE_COLORS.get(lane.current_state, UNKNOWN),
                              (x0, bar_y, max(1, x1 - x0), BAR_HEIGHT))

            # marcadores instantaneos (SEM_WAIT / SEM_SIGNAL / YIELD)
            for mtick, mlabel in lane.markers:
                if mtick < start_tick:
                    continue
                mx = tick_to_x(mtick)
                pygame.draw.line(screen, (255, 255, 255), (mx, bar_y - 4), (mx, bar_y + BAR_HEIGHT + 4), 1)
                mtxt = font_small.render(mlabel[:4], True, (200, 200, 255))
                screen.blit(mtxt, (mx + 2, bar_y - 16))

            pygame.draw.rect(screen, GRID_COLOR, (plot_x0, bar_y, plot_w, BAR_HEIGHT), 1)
            y += LANE_HEIGHT + LANE_GAP

        # eixo de tempo (ticks)
        axis_y = y + 5
        pygame.draw.line(screen, GRID_COLOR, (plot_x0, axis_y), (plot_x1, axis_y), 1)
        n_marks = 8
        for i in range(n_marks + 1):
            t = start_tick + span * i / n_marks
            x = tick_to_x(t)
            pygame.draw.line(screen, GRID_COLOR, (x, axis_y), (x, axis_y + 6), 1)
            lbl = font_small.render(str(int(t)), True, DIM_TEXT_COLOR)
            screen.blit(lbl, (x - 10, axis_y + 8))

        # barra de status
        status_line = f"{status_text}   |   tick atual: {model.current_tick}   |   eventos recebidos: {model.total_events}"
        if paused:
            status_line += "   |   [PAUSADO - espaco para retomar]"
        status_surf = font_small.render(status_line, True, DIM_TEXT_COLOR)
        screen.blit(status_surf, (20, h - 22))

        help_surf = font_small.render("ESPACO=pausa  +/-=zoom  ESC=sair", True, DIM_TEXT_COLOR)
        screen.blit(help_surf, (w - 260, h - 22))

        pygame.display.flip()
        clock.tick(60)

    receiver.stop()
    log_file.close()
    pygame.quit()


if __name__ == "__main__":
    main()