

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/internet-module.h"
#include "ns3/energy-module.h"
#include "ns3/applications-module.h"

#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string>

using namespace ns3;

int sock = 0;
bool socketConnected = false;

// SEND DATA TO PYTHON
void SendData(std::string type, int nodeId, double value, double time)
{
    if (!socketConnected) return;
    std::string msg = type + "," + std::to_string(nodeId) + "," +
                      std::to_string(value) + "," + std::to_string(time) + "\n";
    send(sock, msg.c_str(), msg.size(), 0);
}

//ENERGY CALLBACK
void EnergyChanged(int nodeId, double oldVal, double newVal)
{
    SendData("ENERGY", nodeId, newVal, Simulator::Now().GetSeconds());
}

// ------------------- PACKET RX CALLBACK ----------------------
void PacketReceived(int nodeId, Ptr<const Packet> p)
{
    SendData("PKT", nodeId, p->GetSize(), Simulator::Now().GetSeconds());
}

//MAIN //
int main(int argc, char *argv[])
{
    // Real-time mode
    GlobalValue::Bind("SimulatorImplementationType",
                      StringValue("ns3::RealtimeSimulatorImpl"));

    // Connect to Python dashboard
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) >= 0)
    {
        sockaddr_in serv {};
        serv.sin_family = AF_INET;
        serv.sin_port = htons(5555);
        inet_pton(AF_INET, "127.0.0.1", &serv.sin_addr);

        if (connect(sock, (sockaddr*)&serv, sizeof(serv)) >= 0)
        {
            socketConnected = true;
            NS_LOG_UNCOND("Connected to Python Dashboard!");
        }
    }

    // Topology//
    NodeContainer nodes;
    nodes.Create(2);

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211n);

    YansWifiPhyHelper phy;
    phy.SetChannel(YansWifiChannelHelper::Default().Create());

    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");

    NetDeviceContainer devs = wifi.Install(phy, mac, nodes);

    // Mobility
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    // IP Stack
    InternetStackHelper internet;
    internet.Install(nodes);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer iface = ipv4.Assign(devs);

    //Traffic (OnOff) 
    PacketSinkHelper sink("ns3::UdpSocketFactory",
        InetSocketAddress(Ipv4Address::GetAny(), 9));
    sink.Install(nodes.Get(0)).Start(Seconds(0.0));

    OnOffHelper onoff("ns3::UdpSocketFactory",
        InetSocketAddress(iface.GetAddress(0), 9));
    onoff.SetAttribute("DataRate", StringValue("1Mbps"));
    onoff.SetAttribute("PacketSize", UintegerValue(1024));
    onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=2]"));
    onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    onoff.Install(nodes.Get(1)).Start(Seconds(1.0));

    //ENERGY MODEL
    BasicEnergySourceHelper sourceHelper;
    sourceHelper.Set("BasicEnergySourceInitialEnergyJ", DoubleValue(10.0));
    EnergySourceContainer sources = sourceHelper.Install(nodes);

    WifiRadioEnergyModelHelper energyHelper;
    energyHelper.Install(devs, sources);

    for (uint32_t i = 0; i < nodes.GetN(); i++)
    {
        Ptr<BasicEnergySource> s = DynamicCast<BasicEnergySource>(sources.Get(i));
        s->TraceConnectWithoutContext("RemainingEnergy",
            MakeBoundCallback(&EnergyChanged, i));
    }

    // Packet RX trace
    devs.Get(0)->GetObject<WifiNetDevice>()->GetPhy()->TraceConnectWithoutContext(
        "PhyRxEnd", MakeBoundCallback(&PacketReceived, 0));

    Simulator::Stop(Seconds(20.0));
    Simulator::Run();
    Simulator::Destroy();

    if (socketConnected) close(sock);

    return 0;
}