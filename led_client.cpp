#include <qcc/platform.h>
#include <alljoyn/AllJoynStd.h>
#include <alljoyn/version.h>
#include <alljoyn/Status.h>
#include <alljoyn/BusAttachment.h>

#include <qcc/String.h>

#include <assert.h>
#include <signal.h>

#include <vector>
#include <string>
#include <iostream>

using namespace std;
using namespace qcc;
using namespace ajn;


//TODO: learn what the SessionPort is really about
static const SessionPort SERVICE_PORT = 25;
static const char * SERVICE_PATH="/led"; // WHAT IS THIS??

// exit gracefully on SIGINT
static volatile sig_atomic_t s_interrupt = false;
static void SigIntHandler(int sig)
{
    s_interrupt = true;
}


static bool s_joinComplete = false;
static SessionId s_sessionId = 0;

static BusAttachment *s_msgBus = NULL;

class LedConsumerBusListener : public BusListener, public SessionListener
{
  private:
    string serviceName;

    inline const char *fixNullOwner(const char *ownerName) {
        if (ownerName) 
            return ownerName;
        else 
            return "<none>";
    }

  public:
    LedConsumerBusListener(string serviceName)
        : serviceName(serviceName) {}
    void NameOwnerChanged(const char *busName, const char *previousOwner,
            const char *newOwner)
    {
        if (newOwner && (0==strcmp(busName, serviceName.c_str()))) {
            cout << "NameOwnerChanged: name=" << busName;
            cout << ", oldOwner=" << fixNullOwner(previousOwner);
            cout << ", newOwner=" << fixNullOwner(newOwner);
            cout << endl;
        }
    }

    void FoundAdvertisedName(const char *name, TransportMask transport,
        const char *namePrefix)
    {
        if (0 == strcmp(name, serviceName.c_str())) {
            cout << "Found advertised name: "<< name << "prefix: "<<namePrefix <<endl;

            s_msgBus->EnableConcurrentCallbacks();
            SessionOpts opts(SessionOpts::TRAFFIC_MESSAGES, false, 
                SessionOpts::PROXIMITY_ANY, TRANSPORT_ANY);
            QStatus status = s_msgBus->JoinSession(name, SERVICE_PORT, this, s_sessionId, opts);
            if (ER_OK == status) {
                cout << "Successfully joined session " << s_sessionId << endl;
                s_joinComplete = true;
            } else {
                cout << "Failed to join session ("<<QCC_StatusText(status) <<")"<<endl;
            }
        }
    }
};
static LedConsumerBusListener *s_busListener = NULL;

QStatus CreateInterface(string interfaceName)
{
    InterfaceDescription *intf = NULL;
    QStatus status = s_msgBus->CreateInterface(interfaceName.c_str(), intf);

    if (ER_OK == status) {
        cout << "Created message interface" <<endl;
        intf->AddMethod("on", "y", "s", "pinNum, ackStr", 0);
        intf->AddMethod("off", "y", "s", "pinNum, ackStr", 0);
        intf->Activate();
    } else {
        cout << "Could not create interface '"<< interfaceName <<"'" << endl;
    }

    return status;
}


QStatus StartMessageBus()
{
    QStatus status = s_msgBus->Start();
    // TODO: error messages
    return status;
}

QStatus ConnectToBus()
{
    QStatus status = s_msgBus->Connect();

    //TODO: error messages
    return status;
}

QStatus RegisterBusListener(string serviceName)
{
    s_busListener = new LedConsumerBusListener(serviceName);
    s_msgBus->RegisterBusListener(*s_busListener);
    cout << "Registered bus listener" << endl;
    return ER_OK;
}

QStatus FindAdvertisedName(string serviceName)
{
    QStatus status = s_msgBus->FindAdvertisedName(serviceName.c_str());
    // TODO: error messages
    return status;
}

QStatus WaitForSessionJoinCompletion()
{
    while (!s_joinComplete && !s_interrupt) {
        usleep(100*1000);
    }
    return s_joinComplete && !s_interrupt ? ER_OK : 
        ER_ALLJOYN_JOINSESSION_REPLY_CONNECT_FAILED;
}

QStatus MakeMethodCall(string serviceName, string interfaceName, int pin, bool state)
{
    ProxyBusObject remoteObj(*s_msgBus, serviceName.c_str(), SERVICE_PATH, s_sessionId);
    const InterfaceDescription *intf = s_msgBus->GetInterface(interfaceName.c_str());

    assert(intf);

    remoteObj.AddInterface(*intf);

    Message reply(*s_msgBus);
    MsgArg inputs[1];

    inputs[0].Set("y", pin);

    string messageName = state ? "on" : "off";

    QStatus status = remoteObj.MethodCall(serviceName.c_str(),
        messageName.c_str(), inputs, 1, reply, 5000);

    if (ER_OK == status) {
        cout << serviceName << ".on returned '"<< reply->GetArg(0)->v_string.str;
        cout << "'" <<endl;
    } else {
        cout << "Method call to " << serviceName << ".on failed" << endl;
    }

    return status;
}


int main(int argc, char **argv, char **envArg)
{
    cout << "Starting LED client" << endl;

    signal(SIGINT, SigIntHandler);

    if (argc < 2) {
       cout << argv[0] << " <pin> [<state>]" << endl;
       return -1;
    }

    int pinNum = atoi(argv[1]);
    bool pinState = true;
    if (argc > 2)
        pinState = !(atoi(argv[2]) == 0);


        

    QStatus status = ER_OK;

    string interfaceName("iot.example.led");
    string serviceName("iot.example.led");

    s_msgBus = new BusAttachment("ledController", true);

    if (!s_msgBus) {
        status = ER_OUT_OF_MEMORY;
    }

    if (ER_OK == status) {
        status = CreateInterface(interfaceName);
    }

    if (ER_OK == status) {
        status = StartMessageBus();
    }

    if (ER_OK == status) {
        status = ConnectToBus();
    }

    // Now, finally, we can advertise our LED service
    if (ER_OK == status) {
        RegisterBusListener(serviceName);
        status = FindAdvertisedName(serviceName);
    }

    if (ER_OK == status) {
        status = WaitForSessionJoinCompletion();
    }

    if (ER_OK == status) {
        status = MakeMethodCall(serviceName, interfaceName, pinNum, pinState);
    }


    delete s_msgBus;
    delete s_busListener;

    s_msgBus = NULL;

    cout << "LED consumer shutting down (" << QCC_StatusText(status) <<")"<<endl;
    
    return (int) status;
}

