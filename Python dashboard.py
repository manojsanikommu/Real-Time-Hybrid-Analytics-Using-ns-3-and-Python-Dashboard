import socket
import threading
import pandas as pd
from dash import Dash, dcc, html
from dash.dependencies import Input, Output
import plotly.graph_objs as go

HOST = "127.0.0.1"
PORT = 5555

energy_data = []
packet_events = []
pps_data = {}


#SOCKET SERVER THREAD

def socket_thread():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind((HOST, PORT))
    s.listen(1)

    print(f"Listening on {HOST}:{PORT}...")
    conn, addr = s.accept()
    print("Connected:", addr)

    buffer = ""

    while True:
        data = conn.recv(1024)
        if not data:
            break

        buffer += data.decode()

        while "\n" in buffer:
            line, buffer = buffer.split("\n", 1)
            line = line.strip()

            # Skip empty lines
            if not line:
                continue

            try:
                parts = line.split(",")

                # Validate packet format: TYPE,NODE,VALUE,TIME
                if len(parts) != 4:
                    print("Skipping malformed (wrong parts count):", line)
                    continue

                t, node, val, ts = parts

                # Validate TYPE
                if t not in ("ENERGY", "PKT"):
                    print("Skipping malformed (invalid type):", line)
                    continue

                # Validate conversion
                try:
                    node = int(node)
                    val = float(val)
                    ts = float(ts)
                except:
                    print("Skipping malformed (conversion failed):", line)
                    continue

                # Store validated data
                if t == "ENERGY":
                    energy_data.append({"node": node, "time": ts, "value": val})

                elif t == "PKT":
                    packet_events.append({"node": node, "time": ts, "size": val})

            except Exception as e:
                print("Skipping malformed line:", line)
                continue


# Start socket thread
thr = threading.Thread(target=socket_thread, daemon=True)
thr.start()


#DASH APP LAYOUT

app = Dash(__name__)

app.layout = html.Div([
    html.H1("Milestone 3 Dashboard – Live Energy & Throughput"),

    html.Div([
        html.Div([
            html.H3("Live Energy Depletion (Joules)"),
            dcc.Graph(id="energy-graph"),
        ], style={"width": "48%", "display": "inline-block"}),

        html.Div([
            html.H3("Real Throughput Per Second (Bytes/sec) – Bar Graph"),
            dcc.Graph(id="throughput-graph"),
        ], style={"width": "48%", "display": "inline-block"}),
    ]),

    dcc.Interval(id="interval", interval=1000, n_intervals=0)
])


#THROUGHPUT CALCULATION (Bytes per Second)

def compute_throughput():
    global pps_data
    pps = {}

    for p in packet_events:
        sec = int(p["time"])
        pps.setdefault(sec, 0)
        pps[sec] += p["size"]

    pps_data = pps


#DASH CALLBACK

@app.callback(
    [Output("energy-graph", "figure"),
     Output("throughput-graph", "figure")],
    [Input("interval", "n_intervals")]
)
def update(n):

    compute_throughput()

    #ENERGY GRAPH
    df_e = pd.DataFrame(energy_data)
    fig_energy = go.Figure()

    if not df_e.empty:
        for node in df_e["node"].unique():
            dn = df_e[df_e["node"] == node]
            fig_energy.add_trace(go.Scatter(
                x=dn["time"], y=dn["value"],
                mode="lines+markers",
                name=f"Node {node}"
            ))

    fig_energy.update_layout(
        xaxis_title="Simulation Time (s)",
        yaxis_title="Energy (J)",
        template="plotly_white"
    )

    #THROUGHPUT GRAPH
    x = sorted(pps_data.keys())
    y = [pps_data[t] for t in x]

    fig_tp = go.Figure()
    fig_tp.add_trace(go.Bar(
        x=x,
        y=y,
        name="Bytes/sec",
        marker_color="#4287f5"
    ))

    fig_tp.update_layout(
        xaxis_title="Simulation Time (s)",
        yaxis_title="Bytes/sec",
        template="plotly_white"
    )

    return fig_energy, fig_tp



#RUN THE DASH APP

if __name__ == "__main__":
    app.run(debug=True, use_reloader=False)
