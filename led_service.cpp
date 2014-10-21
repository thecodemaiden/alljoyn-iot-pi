#include <qcc/platform.h>
#include <alljoyn/BusAttachment.h>
#include <alljoyn/BusObject.h>
#include <alljoyn/MsgArg.h>
#include <alljoyn/DBusStd.h>
#include <alljoyn/AllJoynStd.h>
#include <alljoyn/version.h>
#include <alljoyn/Status.h>

#include <assert.h>
#include <signal.h>

#include <string>
#include <iostream>
#include <sstream>
#include <set>

#include <wiringPi.h>

using namespace std;
using namespace qcc;
using namespace ajn;


//TODO: learn what the SessionPort is really about
static const SessionPort SERVICE_PORT = 25;

// exit gracefully on SIGINT
static volatile sig_atomic_t s_interrupt = false;
static void SigIntHandler(int sig)
{
    s_interrupt = true;
}

class LedControllerObject : public BusObject {
    private:
        string serviceName;
        std::set<int> pins;
    public:
        LedControllerObject(BusAttachment &bus, const char *path, string serviceName)
            :BusObject(path), serviceName(serviceName)
        {
            // register interface, method handlers
            const InterfaceDescription *intf = bus.GetInterface(serviceName.c_str());
            assert(intf);
            AddInterface(*intf);

            const MethodEntry methodEntries[] = {
              { intf->GetMember("on"), 
                static_cast<MessageReceiver::MethodHandler>(&LedControllerObject::LightOn) },
              { intf->GetMember("off"), 
                static_cast<MessageReceiver::MethodHandler>(&LedControllerObject::LightOff) }
            };
            QStatus status = AddMethodHandlers(methodEntries, sizeof(methodEntries)/sizeof(methodEntries[0]));
            if (ER_OK != status) {
                cout << "Failed to register method handlers for LED controller" << endl;
            }
        }

        void Setup() {
            AddPin(4);
            AddPin(5);
            AddPin(6);
        }

        void AddPin(int pinNumber) {
            // TODO: validate the pin number
            if (!pins.insert(pinNumber).second){
                // new pin
                pinMode(pinNumber, OUTPUT);
                digitalWrite(pinNumber, HIGH);
                delay(100);
            }
        }

        void SetPinOn(int pinNumber, bool on) {
            // silently ignore unknown pins
            if (pins.count(pinNumber) == 1) {
                int newVal = on ? HIGH : LOW;
                digitalWrite(pinNumber, newVal);    
                delay(100);
            }
        }

        void ObjectRegistered() 
        {
            BusObject::ObjectRegistered();
            cout << "Object registered" << endl;
        }

        void LightOn(const InterfaceDescription::Member *member, Message &msg)
        {
            uint8_t pinNumber = msg->GetArg(0)->v_byte;
            MsgArg outArg("s", "ACK");
            cout << "Turn on pin " << (int)pinNumber << endl;

            SetPinOn(pinNumber, true);
            
            QStatus status = MethodReply(msg, &outArg, 1);
            if (ER_OK != status) {
                cout << "Error sending reply" << endl;
            }
        }

        void LightOff(const InterfaceDescription::Member *member, Message &msg)
        {
            uint8_t pinNumber = msg->GetArg(0)->v_byte;
            MsgArg outArg("s", "ACK");
            cout << "Turn off pin " << (int)pinNumber << endl;

            SetPinOn(pinNumber, false);
            
            QStatus status = MethodReply(msg, &outArg, 1);
            if (ER_OK != status) {
                cout << "Error sending reply" << endl;
            }
        }
};

class LedControllerBusListener : public BusListener, public SessionPortListener
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
    LedControllerBusListener(string serviceName)
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

    bool AcceptSessionJoiner(SessionPort sessionPort, const char *joiner,
            const SessionOpts &opts)
    {
        if (sessionPort != SERVICE_PORT) {
            cout << "Refecting join attempt on unexpected session port ";
            cout << sessionPort << endl;
            return false;
        }
        cout << "Accepting join request from " << joiner << endl;
        return true;
    }
};

static LedControllerBusListener *s_busListener;
static BusAttachment *s_msgBus = NULL;
            
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

QStatus RegisterBusObject(LedControllerObject *obj)
{
    QStatus status = s_msgBus->RegisterBusObject(*obj);
    if (ER_OK == status) {
        cout << "Registered bus object" << endl;
    } else {
        cout << "Failed to register bus object (" << QCC_StatusText(status);
        cout << ")" << endl;
    }
    return status;
}

QStatus StartMessageBus()
{
    QStatus status = s_msgBus->Start();
    // TODO: error messages
    return status;
}

QStatus CreateSession(TransportMask mask)
{
    SessionOpts opts(SessionOpts::TRAFFIC_MESSAGES, false, 
        SessionOpts::PROXIMITY_ANY, mask);
    SessionPort sp = SERVICE_PORT;
    QStatus status = s_msgBus->BindSessionPort(sp, opts, *s_busListener);
    // TODO: error messages
    return status;
}

QStatus AdvertiseName(TransportMask mask, string serviceName)
{
    QStatus status = s_msgBus->AdvertiseName(serviceName.c_str(), mask);
    //TODO: error messages
    return status;
}

QStatus RequestName(string serviceName)
{
    // NOTE: What is this DBus stuff???
    const uint32_t flags = DBUS_NAME_FLAG_REPLACE_EXISTING |
        DBUS_NAME_FLAG_DO_NOT_QUEUE;
    QStatus status = s_msgBus->RequestName(serviceName.c_str(), flags);
    // TODO: error messages
    return status;
}

QStatus ConnectBusAttachment()
{
    QStatus status = s_msgBus->Connect("unix:abstract=alljoyn");
    // TODO: error messages
    return status;
}

void waitForSigInt()
{
    while (s_interrupt == false) {
        usleep(100*1000);
    }
}

int main(int argc, char **argv, char **envArg)
{
    cout << "Starting LED controller service" << endl;

    signal(SIGINT, SigIntHandler);

    QStatus status = ER_OK;

    if (wiringPiSetup() < 0) {
        status = ER_OUT_OF_MEMORY;
    }
      

    string interfaceName("iot.example.led");
    string serviceName("iot.example.led");

    s_busListener = new LedControllerBusListener(serviceName);

    s_msgBus = new BusAttachment("ledController", true);

    if (!s_msgBus) {
        status = ER_OUT_OF_MEMORY;
    }

    if (ER_OK == status) {
        status = CreateInterface(interfaceName);
    }

    if (ER_OK == status) {
        s_msgBus->RegisterBusListener(*s_busListener);
    }

    if (ER_OK == status) {
        status = StartMessageBus();
    }

    LedControllerObject obj(*s_msgBus, "/led", serviceName);
    obj.Setup();

    if (ER_OK == status) {
        status = RegisterBusObject(&obj);
    }

    if (ER_OK == status) {
        status = ConnectBusAttachment();
    }

    // Now, finally, we can advertise our LED service
    if (ER_OK == status) {
        status = RequestName(serviceName);
    }

    const TransportMask serviceTransportMask = TRANSPORT_ANY;

    if (ER_OK == status) {
        status = CreateSession(serviceTransportMask);
    }

    if (ER_OK == status) {
        status = AdvertiseName(serviceTransportMask, serviceName);
    }

    if (ER_OK == status) {
        waitForSigInt();
    }

    delete s_msgBus;
    delete s_busListener;

    s_msgBus = NULL;

    cout << "LED service shutting down (" << QCC_StatusText(status) <<")"<<endl;
    
    return (int) status;
}

