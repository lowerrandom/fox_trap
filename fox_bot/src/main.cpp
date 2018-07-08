//************************************************************
// this is a simple example that uses the painlessMesh library to
// setup a node that logs to a central logging node
// The logServer example shows how to configure the central logging nodes
//************************************************************
extern "C"
{
  #include <user_interface.h>
}

#include <painlessMesh.h>
#include <cstdint>


//Sniffer stuff
#define DATA_LENGTH           112
#define TYPE_MANAGEMENT       0x00
#define TYPE_CONTROL          0x01
#define TYPE_DATA             0x02
#define SUBTYPE_PROBE_REQUEST 0x08

#define SENSOR_ID             0x00
#define DISABLE 0
#define ENABLE  1
#define TIME_PRECISION_MS = 10

//Wifi Configuration
#define   MESH_PREFIX     "dc719"
#define   MESH_PASSWORD   "whatdoesthefoxsay?"
#define   MESH_PORT       5566

//Scheduling Intervals
#define CHANNEL_HOP_INTERVAL_MS   100
#define RX_COMM_INTERVAL 10000

//mesh members...NO_OBJECTS == NO_HOPE
void botInitialization();
void channelHop();
void receivedCallback( uint32_t from, String &msg );
uint32_t CalculateSynchronizationDelay();
// uint8_t


// void receivedCallback( uint31_t from, String &msg );
Scheduler     _userScheduler; // to control your personal task
painlessMesh  _mesh;
Task _botInitializationTask(TASK_IMMEDIATE, TASK_ONCE, &botInitialization, &_userScheduler);
Task channelHopTask(CHANNEL_HOP_INTERVAL_MS, TASK_FOREVER, &channelHop);
Task snifferInitialization();
uint32_t roundUp(uint32_t numToRound, uint32_t multiple);


// Prototype
uint16_t _channel = 6;
size_t logServerId = 0;
int32_t _startTime = 0;
uint32_t _synchInterval = 5000; //ms
unsigned long _initDelay = 15000000;


//*************** DEMARC SNIFFER ***************/
struct RxControl {
 signed rssi:8; // signal intensity of packet
 unsigned rate:4;
 unsigned is_group:1;
 unsigned:1;
 unsigned sig_mode:2; // 0:is 11n packet; 1:is not 11n packet;
 unsigned legacy_length:12; // if not 11n packet, shows length of packet.
 unsigned damatch0:1;
 unsigned damatch1:1;
 unsigned bssidmatch0:1;
 unsigned bssidmatch1:1;
 unsigned MCS:7; // if is 11n packet, shows the modulation and code used (range from 0 to 76)
 unsigned CWB:1; // if is 11n packet, shows if is HT40 packet or not
 unsigned HT_length:16;// if is 11n packet, shows length of packet.
 unsigned Smoothing:1;
 unsigned Not_Sounding:1;
 unsigned:1;
 unsigned Aggregation:1;
 unsigned STBC:2;
 unsigned FEC_CODING:1; // if is 11n packet, shows if is LDPC packet or not.
 unsigned SGI:1;
 unsigned rxend_state:8;
 unsigned ampdu_cnt:8;
 unsigned channel:4; //which channel this packet in.
 unsigned:12;
};

struct SnifferPacket{
    struct RxControl rx_ctrl;
    uint8_t data[DATA_LENGTH];
    uint16_t cnt;
    uint16_t len;
};

static void getMAC(char *addr, uint8_t* data, uint16_t offset) {
  sprintf(addr, "%02x:%02x:%02x:%02x:%02x:%02x", data[offset+0], data[offset+1], data[offset+2], data[offset+3], data[offset+4], data[offset+5]);
}

static void showMetadata(SnifferPacket *snifferPacket) {

  // Serial.printf("Attempting to show metadata..\n");
  unsigned int frameControl = ((unsigned int)snifferPacket->data[1] << 8) + snifferPacket->data[0];

  uint8_t version      = (frameControl & 0b0000000000000011) >> 0;
  uint8_t frameType    = (frameControl & 0b0000000000001100) >> 2;
  uint8_t frameSubType = (frameControl & 0b0000000011110000) >> 4;
  uint8_t toDS         = (frameControl & 0b0000000100000000) >> 8;
  uint8_t fromDS       = (frameControl & 0b0000001000000000) >> 9;

  // Only look for probe request packets
  if (frameType != TYPE_MANAGEMENT ||
      frameSubType != SUBTYPE_PROBE_REQUEST)
        return;
  Serial.print(" Sensor #: ");
  Serial.print(SENSOR_ID);

  Serial.print("RSSI: ");
  Serial.print(snifferPacket->rx_ctrl.rssi, DEC);

  Serial.print(" Ch: ");
  Serial.print(wifi_get_channel());

  char addr[] = "00:00:00:00:00:00";
  getMAC(addr, snifferPacket->data, 10);
  Serial.print(" BSSID: ");
  Serial.print(addr);

  //uint8_t SSID_length = snifferPacket->data[25];
  //Serial.print(" SSID: ");
  //printDataSpan(26, SSID_length, snifferPacket->data);

  Serial.println();
}

/**
 * Callback for promiscuous mode
 */
static void ICACHE_FLASH_ATTR sniffer_callback(uint8_t *buffer, uint16_t length) {
  struct SnifferPacket *snifferPacket = (struct SnifferPacket*) buffer;
  showMetadata(snifferPacket);
}

static void printDataSpan(uint16_t start, uint16_t size, uint8_t* data) {
  for(uint16_t i = start; i < DATA_LENGTH && i < start+size; i++) {
    Serial.write(data[i]);
  }
}
/**
 * Callback for channel hoping
 */
void channelHop()
{
  // hoping channels 1-14
  uint8 new_channel = wifi_get_channel() + 1;
  if (new_channel > 14)
    new_channel = 1;
  wifi_set_channel(new_channel);
}
//*************** DEMARC SNIFFER ***************/


// Send message to the logServer every 10 seconds
Task myLoggingTask(RX_COMM_INTERVAL, TASK_FOREVER, []() {
    DynamicJsonBuffer jsonBuffer;
    JsonObject& msg = jsonBuffer.createObject();
    msg["topic"] = "sensor";
    msg["value"] = random(0, 180);

    String str;
    msg.printTo(str);
    if (logServerId == 0) // If we don't know the logServer yet
        _mesh.sendBroadcast(str);
    else
        _mesh.sendSingle(logServerId, str);

    // log to serial
    //msg.printTo(Serial);
    //Serial.printf("\n");
});

void botInitialization()
{
    Serial.printf("BOT:botInitialization");

    _mesh.setDebugMsgTypes(ERROR | CONNECTION);  // set before init() so that you can see startup messages
    _mesh.init( MESH_PREFIX, MESH_PASSWORD, &_userScheduler, MESH_PORT, WIFI_AP_STA, _channel);
    //keep the topology from fucking itself
    _mesh.setRoot(false);
    // This and all other mesh should ideally now the mesh contains a root
    _mesh.setContainsRoot(true);
    _mesh.onReceive(&receivedCallback);

    //Wait for a specified amount of time to gather the longest RTT from c2..not a brillaint design decision
    _botInitializationTask.delay(_initDelay);
    //instead of using an async delay, delay all execution so that the sniffer continues its job
    Serial.printf("BOT:botInitialization_END");
}

void setup() {
  Serial.begin(115200);
  // _botInitializationTask new Task(TASK_IMMEDIATE, TASK_ONCE, &botInitialization, &_userScheduler);
  // Serial.printf("logClient ID:%u",_mesh.getNodeId());
  _userScheduler.addTask(_botInitializationTask);
    //Calculate synch delay, get offset of delay to current time and either use set or restart
  _botInitializationTask.restartDelayed(CalculateSynchronizationDelay());
  _botInitializationTask.enable();
  Serial.printf("BOT:SETUP with start time of %i", _startTime);

  // Add the task to the your scheduler
  // _userScheduler.addTask(myLoggingTask);
  // _userScheduler.addTask(channelHopTask);
  // myLoggingTask.enable();
  // channelHopTask.enable();

  //SNIFFER
  // delay(10);
  // wifi_set_opmode(STATION_MODE);
  // wifi_set_channel(1);
  // wifi_promiscuous_enable(DISABLE);
  // Serial.printf("promiscuos disable\n");
  // wifi_set_promiscuous_rx_cb(sniffer_callback);
  // Serial.printf("promiscuos callback set\n");
  // wifi_promiscuous_enable(ENABLE);
  // Serial.printf("promiscuos enable\n");
}

void loop() {
    _userScheduler.execute(); // it will run _mesh scheduler as well
    _mesh.update();
}

void receivedCallback( uint32_t from, String &msg ) {
  Serial.printf("logClient: Received from %u msg=%s\n", from, msg.c_str());

  // Saving logServer
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(msg);
  if (root.containsKey("initialize")) {
          _startTime = (root["initialize"] > _startTime)
            ? root["initialize"]
            : _startTime;
    }
      // Serial.printf("logCleint: Handled from %u msg=%s\n", from, msg.c_str());
}

uint32_t CalculateSynchronizationDelay(){
  // uint32_t NextSynchronizationPeriod();
  //get next sync periods. For now, assume multiples of 5
  uint32_t current = _mesh.getNodeTime();

  //offset current time from the target interval
  //pulling by 1ms
  current = current / 1000;
  uint32_t nextThreshold = roundUp(current, _synchInterval);
  return nextThreshold;
}

//rounds from nearest interval of 10ms (time precision) multiplied by the defined time synchronization
uint32_t roundUp(uint32_t numToRound, uint32_t multiple)
{
    if (multiple == 0)
        return numToRound;

    int remainder = numToRound % multiple;
    if (remainder == 0)
        return numToRound;

    return numToRound + multiple - remainder;
}

uint8_t MostSignificantBit()
{
  uint8_t bytesLen[sizeof(uint32_t)];
  uint32_t blockSize = 535;

 bytesLen[3] = (blockSize >>  0) & 0xFF;
 bytesLen[2] = (blockSize >>  8) & 0xFF;
 bytesLen[1] = (blockSize >> 16) & 0xFF;
 bytesLen[0] = (blockSize >> 24) & 0xFF;

 bool removeZeroes = true;
 // std::cout << "bytesLen: 0x";
 for(size_t i=0; i<sizeof(bytesLen); i++)
 {
   if(bytesLen[i] != 0)
   {
     return bytesLen[i];
     // removeZeroes = false;
   }
//if(!removeZeroes)
   // {
   //   std::cout << std::hex << (int)bytesLen[i];
   // }
 }
 // std::cout << std::endl;
}