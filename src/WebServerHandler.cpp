#include "WebServerHandler.h"
#include <Update.h> // Library OTA

WebServerHandler::WebServerHandler(uint16_t port) : _server(port) {}

void WebServerHandler::begin()
{
    if (!LittleFS.begin(true))
    {
        Serial.println("FATAL: LittleFS Mount Failed!");
        return;
    }
    Serial.println("LittleFS Mounted Successfully.");
    _server.begin();
    Serial.println("Web Server started on port 80.");
}

String WebServerHandler::getContentType(String filename)
{
    if (filename.endsWith(".html"))
        return "text/html";
    else if (filename.endsWith(".css"))
        return "text/css";
    else if (filename.endsWith(".js"))
        return "application/javascript";
    else if (filename.endsWith(".json"))
        return "application/json";
    else if (filename.endsWith(".png"))
        return "image/png";
    else if (filename.endsWith(".jpg"))
        return "image/jpeg";
    else if (filename.endsWith(".ico"))
        return "image/x-icon";
    else if (filename.endsWith(".gz"))
        return "application/x-gzip";
    return "text/plain";
}

// Processor Template HTML
String WebServerHandler::processor(const String &var, EthernetLinkStatus linkStatus)
{
    if (var == "LINK_STATUS")
    {
        if (linkStatus == LinkON)
            return "ONLINE";
        if (linkStatus == LinkOFF)
            return "OFFLINE";
        return "UNKNOWN";
    }
    return String();
}

// ---------------------------------------------------------
// Helper Parsing
// ---------------------------------------------------------
String getParam(String data, String key)
{
    String keyPair = key + "=";
    int start = data.indexOf(keyPair);
    if (start == -1)
        return "";

    start += keyPair.length();
    int end = data.indexOf("&", start);
    if (end == -1)
        end = data.length();

    String value = data.substring(start, end);
    value.replace("+", " ");
    value.replace("%2F", "/");
    value.replace("%3A", ":");
    value.replace("%40", "@");
    return value;
}

String getNum(String data, String key)
{
    String val = getParam(data, key);
    if (val == "")
        return "0";
    return val;
}

String getJsonVal(String json, String key)
{
    String search = "\"" + key + "\":";
    int start = json.indexOf(search);
    if (start == -1)
        return "";
    start += search.length();

    bool isString = (json.charAt(start) == '"');
    if (isString)
        start++;

    int end;
    if (isString)
    {
        end = json.indexOf("\"", start);
    }
    else
    {
        int comma = json.indexOf(",", start);
        int brace = json.indexOf("}", start);
        if (comma == -1)
            end = brace;
        else if (brace == -1)
            end = comma;
        else
            end = min(comma, brace);
    }

    if (end == -1)
        return "";
    return json.substring(start, end);
}

// ---------------------------------------------------------
// Handle Client
// ---------------------------------------------------------
void WebServerHandler::handleClient(EthernetLinkStatus linkStatus)
{
    EthernetClient client = _server.available();

    if (client)
    {
        String reqLine = client.readStringUntil('\r');
        client.readStringUntil('\n');

        if (reqLine.length() > 0)
        {
            String method = reqLine.substring(0, reqLine.indexOf(' '));
            int addr_start = reqLine.indexOf(' ');
            int addr_end = reqLine.indexOf(' ', addr_start + 1);
            String path = reqLine.substring(addr_start + 1, addr_end);

            // Uncomment untuk debug path
            // Serial.print("Req: "); Serial.print(method); Serial.print(" "); Serial.println(path);

            // ============================================================
            // 1. HANDLE POST (Simpan Data & OTA)
            // ============================================================
            if (method == "POST")
            {
                // A. OTA UPDATE
                if (path == "/update")
                {
                    long contentLength = 0;
                    while (client.connected())
                    {
                        String line = client.readStringUntil('\n');
                        if (line == "\r")
                            break;
                        if (line.startsWith("Content-Length: "))
                            contentLength = line.substring(16).toInt();
                    }
                    if (contentLength > 0 && Update.begin(contentLength, U_FLASH))
                    {
                        Update.writeStream(client);
                        if (Update.end(true))
                        {
                            client.println("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nSuccess");
                            delay(100);
                            client.stop();
                            ESP.restart();
                            return;
                        }
                    }
                    client.println("HTTP/1.1 500 Error\r\nConnection: close\r\n\r\nFailed");
                }

                // B. CONFIG SAVE
                else
                {
                    while (client.connected())
                    {
                        String line = client.readStringUntil('\n');
                        if (line == "\r")
                            break;
                    }
                    String body = "";
                    while (client.available())
                        body += (char)client.read();

                    // --- System Settings ---
                    if (body.indexOf("username=") != -1 || body.indexOf("sdInterval=") != -1)
                    {
                        String user = getParam(body, "username");
                        String pass = getParam(body, "password");
                        String interval = getParam(body, "sdInterval");
                        String jsonString = "{\"username\":\"" + user + "\",\"password\":\"" + pass + "\",\"sdInterval\":\"" + interval + "\"}";
                        File file = LittleFS.open("/systemSettings.json", "w");
                        if (file)
                        {
                            file.print(jsonString);
                            file.close();
                        }
                        client.println("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nSystem Saved");
                    }

                    // --- Modbus Config ---
                    else if (body.indexOf("baudrate=") != -1 || body.indexOf("slaveid=") != -1)
                    {
                        String jsonString = "{";
                        jsonString += "\"baudrate\":\"" + getParam(body, "baudrate") + "\",";
                        jsonString += "\"parity\":\"" + getParam(body, "parity") + "\",";
                        jsonString += "\"stopbits\":\"" + getParam(body, "stopbits") + "\",";
                        jsonString += "\"databits\":\"" + getParam(body, "databits") + "\",";
                        jsonString += "\"slaveid\":\"" + getParam(body, "slaveid") + "\",";
                        jsonString += "\"mode\":\"" + getParam(body, "mode") + "\"";
                        jsonString += "}";
                        File file = LittleFS.open("/modbusSetup.json", "w");
                        if (file)
                        {
                            file.print(jsonString);
                            file.close();
                        }
                        client.println("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nModbus Saved");
                    }

                    // --- Digital IO Config (Partial) ---
                    else if (body.indexOf("inputPin=") != -1)
                    {
                        String targetKey = getParam(body, "inputPin");
                        targetKey.replace(" ", "");
                        String jsonContent = "{}";
                        if (LittleFS.exists("/configDigital.json"))
                        {
                            File f = LittleFS.open("/configDigital.json", "r");
                            if (f)
                            {
                                jsonContent = f.readString();
                                f.close();
                            }
                        }
                        String inv = (body.indexOf("inputInversion=") != -1) ? "1" : "0";
                        String newBlock = "\"" + targetKey + "\":{";
                        newBlock += "\"name\":\"" + getParam(body, "nameDI") + "\",";
                        newBlock += "\"invers\":" + inv + ",";
                        newBlock += "\"taskMode\":\"" + getParam(body, "taskMode") + "\",";
                        newBlock += "\"inputState\":\"" + getParam(body, "inputState") + "\",";
                        newBlock += "\"intervalTime\":" + getNum(body, "intervalTime") + ",";
                        newBlock += "\"conversionFactor\":" + getNum(body, "conversionFactor");
                        newBlock += "}";
                        String searchKey = "\"" + targetKey + "\":";
                        int startPos = jsonContent.indexOf(searchKey);
                        if (startPos != -1)
                        {
                            int openBrace = jsonContent.indexOf("{", startPos);
                            int closeBrace = jsonContent.indexOf("}", openBrace);
                            if (openBrace != -1 && closeBrace != -1)
                            {
                                String oldBlock = jsonContent.substring(startPos, closeBrace + 1);
                                String newValueOnly = searchKey + newBlock.substring(newBlock.indexOf('{'));
                                jsonContent.replace(oldBlock, newValueOnly);
                            }
                        }
                        File f = LittleFS.open("/configDigital.json", "w");
                        if (f)
                        {
                            f.print(jsonContent);
                            f.close();
                        }
                        client.println("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nDigital Saved");
                    }

                    // --- Network & ERP Config ---
                    else if (body.indexOf("ssid=") != -1 || body.indexOf("erpUrl=") != -1)
                    {
                        String jsonC = "{}";
                        if (LittleFS.exists("/configNetwork.json"))
                        {
                            File f = LittleFS.open("/configNetwork.json", "r");
                            if (f)
                            {
                                jsonC = f.readString();
                                f.close();
                            }
                        }
                        String netMode = getJsonVal(jsonC, "networkMode");
                        String ssid = getJsonVal(jsonC, "ssid");
                        String pass = getJsonVal(jsonC, "password");
                        String apSsid = getJsonVal(jsonC, "apSsid");
                        String apPass = getJsonVal(jsonC, "apPassword");
                        String dhcp = getJsonVal(jsonC, "dhcpMode");
                        String ip = getJsonVal(jsonC, "ipAddress");
                        String sub = getJsonVal(jsonC, "subnet");
                        String gw = getJsonVal(jsonC, "ipGateway");
                        String dns = getJsonVal(jsonC, "ipDNS");
                        String sendInt = getJsonVal(jsonC, "sendInterval");
                        String proto = getJsonVal(jsonC, "protocolMode");
                        String endp = getJsonVal(jsonC, "endpoint");
                        String port = getJsonVal(jsonC, "port");
                        String pub = getJsonVal(jsonC, "pubTopic");
                        String subT = getJsonVal(jsonC, "subTopic");
                        String mUser = getJsonVal(jsonC, "mqttUsername");
                        String mPass = getJsonVal(jsonC, "mqttPass");
                        String logMode = getJsonVal(jsonC, "loggerMode");
                        String modMode = getJsonVal(jsonC, "modbusMode");
                        String proto2 = getJsonVal(jsonC, "protocolMode2");
                        String mPort = getJsonVal(jsonC, "modbusPort");
                        String slave = getJsonVal(jsonC, "modbusSlaveID");
                        String trig = getJsonVal(jsonC, "sendTrig");
                        String erpU = getJsonVal(jsonC, "erpUrl");
                        String erpN = getJsonVal(jsonC, "erpUsername");
                        String erpP = getJsonVal(jsonC, "erpPassword");

                        if (body.indexOf("ssid=") != -1)
                        {
                            netMode = getParam(body, "networkMode");
                            ssid = getParam(body, "ssid");
                            pass = getParam(body, "password");
                            apSsid = getParam(body, "apSsid");
                            apPass = getParam(body, "apPassword");
                            dhcp = getParam(body, "dhcpMode");
                            ip = getParam(body, "ipAddress");
                            sub = getParam(body, "subnet");
                            gw = getParam(body, "ipGateway");
                            dns = getParam(body, "ipDNS");
                            sendInt = getParam(body, "sendInterval");
                            proto = getParam(body, "protocolMode");
                            endp = getParam(body, "endpoint");
                            port = getParam(body, "port");
                            pub = getParam(body, "pubTopic");
                            subT = getParam(body, "subTopic");
                            mUser = getParam(body, "mqttUsername");
                            mPass = getParam(body, "mqttPass");
                            logMode = (body.indexOf("loggerMode=") != -1) ? "true" : "false";
                            modMode = (body.indexOf("modbusMode=") != -1) ? "true" : "false";
                            proto2 = getParam(body, "protocolMode2");
                            mPort = getParam(body, "modbusPort");
                            slave = getParam(body, "modbusSlaveID");
                            trig = getParam(body, "sendTrig");
                        }
                        if (body.indexOf("erpUrl=") != -1)
                        {
                            erpU = getParam(body, "erpUrl");
                            erpN = getParam(body, "erpUsername");
                            erpP = getParam(body, "erpPassword");
                        }

                        String j = "{";
                        j += "\"networkMode\":\"" + netMode + "\",\"ssid\":\"" + ssid + "\",\"password\":\"" + pass + "\",\"apSsid\":\"" + apSsid + "\",\"apPassword\":\"" + apPass + "\",";
                        j += "\"dhcpMode\":\"" + dhcp + "\",\"ipAddress\":\"" + ip + "\",\"subnet\":\"" + sub + "\",\"ipGateway\":\"" + gw + "\",\"ipDNS\":\"" + dns + "\",";
                        j += "\"sendInterval\":\"" + sendInt + "\",\"protocolMode\":\"" + proto + "\",\"endpoint\":\"" + endp + "\",\"port\":\"" + port + "\",";
                        j += "\"pubTopic\":\"" + pub + "\",\"subTopic\":\"" + subT + "\",\"mqttUsername\":\"" + mUser + "\",\"mqttPass\":\"" + mPass + "\",";
                        j += "\"loggerMode\":" + logMode + ",\"modbusMode\":" + modMode + ",\"protocolMode2\":\"" + proto2 + "\",\"modbusPort\":\"" + mPort + "\",";
                        j += "\"modbusSlaveID\":\"" + slave + "\",\"sendTrig\":\"" + trig + "\",\"erpUrl\":\"" + erpU + "\",\"erpUsername\":\"" + erpN + "\",\"erpPassword\":\"" + erpP + "\"";
                        j += "}";

                        File f = LittleFS.open("/configNetwork.json", "w");
                        if (f)
                        {
                            f.print(j);
                            f.close();
                        }
                        client.println("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nNetwork Saved");
                    }
                }
            }

            // ============================================================
            // 2. HANDLE GET
            // ============================================================
            else if (method == "GET")
            {
                // --- API: HOME LOAD (BARU - Fokus Home.js) ---
                // if (path == "/homeLoad")
                // {
                //     // 1. Baca Config Network (untuk Header Dashboard)
                //     String netJson = "{}";
                //     if (LittleFS.exists("/configNetwork.json"))
                //     {
                //         File f = LittleFS.open("/configNetwork.json", "r");
                //         if (f)
                //         {
                //             netJson = f.readString();
                //             f.close();
                //         }
                //     }

                //     // 2. Baca Config Digital (untuk Task Mode Labels)
                //     String digJson = "{}";
                //     if (LittleFS.exists("/configDigital.json"))
                //     {
                //         File f = LittleFS.open("/configDigital.json", "r");
                //         if (f)
                //         {
                //             digJson = f.readString();
                //             f.close();
                //         }
                //     }

                //     // 3. Extract Info Dasar
                //     String nMode = getJsonVal(netJson, "networkMode");
                //     String ssid = getJsonVal(netJson, "ssid");
                //     String ip = getJsonVal(netJson, "ipAddress");
                //     String sInt = getJsonVal(netJson, "sendInterval");
                //     String pMode = getJsonVal(netJson, "protocolMode");
                //     String endp = getJsonVal(netJson, "endpoint");

                //     // Cek Status Link
                //     String conn = (linkStatus == LinkON) ? "Connected" : "Disconnected";

                //     // 4. Extract Task Mode (Manual Parse untuk 4 Pin)
                //     // Default Normal jika file tidak ada
                //     String tasks[4] = {"Normal", "Normal", "Normal", "Normal"};
                //     for (int i = 1; i <= 4; i++)
                //     {
                //         String key = "DI" + String(i);
                //         // Cari blok DIx
                //         String searchKey = "\"" + key + "\":";
                //         int startPos = digJson.indexOf(searchKey);
                //         if (startPos != -1)
                //         {
                //             int openB = digJson.indexOf("{", startPos);
                //             int closeB = digJson.indexOf("}", openB);
                //             if (openB != -1 && closeB != -1)
                //             {
                //                 String block = digJson.substring(openB, closeB + 1);
                //                 String val = getJsonVal(block, "taskMode");
                //                 if (val != "")
                //                     tasks[i - 1] = val;
                //             }
                //         }
                //     }

                //     // 5. Rakit JSON Response (Sesuai struktur home.js)
                //     String res = "{";
                //     res += "\"networkMode\":\"" + nMode + "\",";
                //     res += "\"ssid\":\"" + ssid + "\",";
                //     res += "\"ipAddress\":\"" + ip + "\",";
                //     res += "\"macAddress\":\"02:00:00:00:00:01\","; // Hardcode (sesuai main.cpp)
                //     res += "\"connStatus\":\"" + conn + "\",";
                //     res += "\"jobNumber\":\"JOB-001\","; // Dummy
                //     res += "\"sendInterval\":\"" + sInt + "\",";
                //     res += "\"protocolMode\":\"" + pMode + "\",";
                //     res += "\"endpoint\":\"" + endp + "\",";

                //     // DI Object
                //     res += "\"DI\":{";
                //     res += "\"value\":[0,0,0,0],"; // Dummy Sensor Values (0)
                //     res += "\"taskMode\":[\"" + tasks[0] + "\",\"" + tasks[1] + "\",\"" + tasks[2] + "\",\"" + tasks[3] + "\"]";
                //     res += "},";

                //     // AI Object
                //     res += "\"AI\":{";
                //     res += "\"rawValue\":[0,0,0,0],";
                //     res += "\"scaledValue\":[0,0,0,0]";
                //     res += "},";

                //     res += "\"enAI\":[1,1,1,1],";
                //     res += "\"datetime\":\"2024-01-01 12:00:00\"";
                //     res += "}";

                //     client.println("HTTP/1.1 200 OK");
                //     client.println("Content-Type: application/json");
                //     client.println("Connection: close");
                //     client.println();
                //     client.print(res);
                //     client.stop();
                //     return;
                // }


if (path == "/homeLoad")
                {
                    // 1. Baca Config Network
                    String netJson = "{}";
                    if (LittleFS.exists("/configNetwork.json")) {
                        File f = LittleFS.open("/configNetwork.json", "r");
                        if (f) { netJson = f.readString(); f.close(); }
                    }

                    // 2. Baca Config Digital
                    String digJson = "{}";
                    if (LittleFS.exists("/configDigital.json")) {
                        File f = LittleFS.open("/configDigital.json", "r");
                        if (f) { digJson = f.readString(); f.close(); }
                    }

                    String nMode = getJsonVal(netJson, "networkMode");
                    String ssid = getJsonVal(netJson, "ssid");
                    String ip = getJsonVal(netJson, "ipAddress");
                    String sInt = getJsonVal(netJson, "sendInterval");
                    String pMode = getJsonVal(netJson, "protocolMode");
                    String endp = getJsonVal(netJson, "endpoint");
                    String conn = (linkStatus == LinkON) ? "Connected" : "Disconnected";

                    String tasks[4] = {"Normal", "Normal", "Normal", "Normal"};
                    // (Logika parsing taskMode tetap sama...)

                    // 5. Rakit JSON Response dengan DATA MODBUS REAL
                    String res = "{";
                    res += "\"networkMode\":\"" + nMode + "\",";
                    res += "\"ssid\":\"" + ssid + "\",";
                    res += "\"ipAddress\":\"" + ip + "\",";
                    res += "\"macAddress\":\"02:00:00:00:00:01\","; 
                    res += "\"connStatus\":\"" + conn + "\",";
                    res += "\"jobNumber\":\"JOB-001\","; 
                    res += "\"sendInterval\":\"" + sInt + "\",";
                    res += "\"protocolMode\":\"" + pMode + "\",";
                    res += "\"endpoint\":\"" + endp + "\",";

                    // --- BAGIAN INI MENGGUNAKAN DATA MODBUS ---
                    res += "\"DI\":{";
                    // Contoh: Menggunakan register 0-3 sebagai nilai sensor
                    res += "\"value\":[" + String(modbusData[0]) + "," + String(modbusData[1]) + "," + String(modbusData[2]) + "," + String(modbusData[3]) + "],"; 
                    res += "\"taskMode\":[\"" + tasks[0] + "\",\"" + tasks[1] + "\",\"" + tasks[2] + "\",\"" + tasks[3] + "\"]";
                    res += "},";

                    res += "\"AI\":{";
                    // Contoh: Menampilkan nilai mentah yang sama (bisa disesuaikan jika register AI berbeda)
                    res += "\"rawValue\":[" + String(modbusData[0]) + "," + String(modbusData[1]) + "," + String(modbusData[2]) + "," + String(modbusData[3]) + "],";
                    res += "\"scaledValue\":[" + String(modbusData[0]) + "," + String(modbusData[1]) + "," + String(modbusData[2]) + "," + String(modbusData[3]) + "]";
                    res += "},";
                    // ------------------------------------------

                    res += "\"enAI\":[1,1,1,1],";
                    res += "\"datetime\":\"2024-01-01 12:00:00\"";
                    res += "}";

                    client.println("HTTP/1.1 200 OK");
                    client.println("Content-Type: application/json");
                    client.println("Connection: close");
                    client.println();
                    client.print(res);
                    client.stop();
                    return;
                }

                // --- API Config Loads ---
                else if (path == "/settingsLoad")
                    path = "/systemSettings.json";
                else if (path == "/modbusLoad")
                    path = "/modbusSetup.json";
                else if (path.indexOf("/networkLoad") != -1)
                {
                    if (path.indexOf("restart=1") != -1)
                    {
                        client.println("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nRestarting...");
                        client.stop();
                        delay(500);
                        ESP.restart();
                        return;
                    }
                    path = "/configNetwork.json";
                }

                // --- API Digital Load ---
                else if (path.indexOf("/digitalLoad") != -1)
                {
                    int qMark = path.indexOf('?');
                    String query = (qMark != -1) ? path.substring(qMark + 1) : "";
                    String inputVal = getParam(query, "input");
                    if (inputVal != "")
                    {
                        String targetKey = "DI" + inputVal;
                        String jsonContent = "{}";
                        File f = LittleFS.open("/configDigital.json", "r");
                        if (f)
                        {
                            jsonContent = f.readString();
                            f.close();
                        }

                        String searchKey = "\"" + targetKey + "\":";
                        int startPos = jsonContent.indexOf(searchKey);
                        String pinBlock = "";
                        if (startPos != -1)
                        {
                            int openBrace = jsonContent.indexOf("{", startPos);
                            int closeBrace = jsonContent.indexOf("}", openBrace);
                            if (openBrace != -1 && closeBrace != -1)
                                pinBlock = jsonContent.substring(openBrace, closeBrace + 1);
                        }
                        String name = getJsonVal(pinBlock, "name");
                        String inv = getJsonVal(pinBlock, "invers");
                        String tm = getJsonVal(pinBlock, "taskMode");
                        String is = getJsonVal(pinBlock, "inputState");
                        String it = getJsonVal(pinBlock, "intervalTime");
                        String cf = getJsonVal(pinBlock, "conversionFactor");

                        String respJson = "{";
                        respJson += "\"nameDI\":\"" + name + "\",\"invDI\":" + (inv == "" ? "0" : inv) + ",";
                        respJson += "\"taskMode\":\"" + tm + "\",\"inputState\":\"" + is + "\",";
                        respJson += "\"intervalTime\":" + (it == "" ? "0" : it) + ",\"conversionFactor\":" + (cf == "" ? "0" : cf);
                        respJson += "}";
                        client.println("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n" + respJson);
                        client.stop();
                        return;
                    }
                    else
                    {
                        path = "/configDigital.json";
                    }
                }

                // --- API OTA Status ---
                else if (path == "/updateStatus")
                {
                    String json = "{";
                    json += "\"freeHeap\":" + String(ESP.getFreeHeap()) + ",";
                    json += "\"sketchSize\":" + String(ESP.getSketchSize()) + ",";
                    json += "\"freeSketchSpace\":" + String(ESP.getFreeSketchSpace());
                    json += "}";
                    client.println("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n" + json);
                    client.stop();
                    return;
                }

                else if (path == "/getTime")
                {
                    client.println("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"datetime\":\"2024-01-01 12:00:00\"}");
                    client.stop();
                    return;
                }
                else if (path == "/getValue")
                {
                    client.println("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n[]");
                    client.stop();
                    return;
                }

                // --- Routing Pages ---
                if (path == "/")
                    path = "/home.html";
                else if (path == "/home")
                    path = "/home.html";
                else if (path == "/network")
                    path = "/network.html";
                else if (path == "/analog_input")
                    path = "/analog_input.html";
                else if (path == "/digital_IO")
                    path = "/digital_IO.html";
                else if (path == "/modbus_setup")
                    path = "/modbus_setup.html";
                else if (path == "/system_settings")
                    path = "/system_settings.html";
                else if (path == "/updateOTA")
                    path = "/UpdateOTA.html";

                // --- Routing JS ---
                if (path.indexOf("network.js") != -1)
                    path = "/js/network.js";
                if (path.indexOf("system_setting.js") != -1)
                    path = "/js/system_setting.js";
                if (path.indexOf("modbus_setup.js") != -1)
                    path = "/js/modbus_setup.js";
                if (path.indexOf("digital_IO.js") != -1)
                    path = "/js/digital_IO.js";
                if (path.indexOf("UpdateOTA.js") != -1)
                    path = "/js/UpdateOTA.js";
                if (path.indexOf("home.js") != -1)
                    path = "/js/home.js"; // Tambahan JS Home

                // --- Serve File ---
                String pathWithGz = path + ".gz";
                bool fileFound = false;
                bool isGzipped = false;

                if (LittleFS.exists(pathWithGz))
                {
                    fileFound = true;
                    isGzipped = true;
                    path = pathWithGz;
                }
                else if (LittleFS.exists(path))
                {
                    fileFound = true;
                    isGzipped = false;
                }

                if (fileFound)
                {
                    File file = LittleFS.open(path, "r");
                    String filenameForMime = isGzipped ? path.substring(0, path.length() - 3) : path;
                    String dataType = getContentType(filenameForMime);

                    client.println("HTTP/1.1 200 OK");
                    client.println("Content-Type: " + dataType);
                    if (isGzipped)
                        client.println("Content-Encoding: gzip");
                    client.println("Connection: close");
                    client.println();

                    if (filenameForMime.endsWith(".html") && !isGzipped)
                    {
                        while (file.available())
                        {
                            String line = file.readStringUntil('\n');
                            if (line.indexOf("{{") != -1 && line.indexOf("}}") != -1)
                            {
                                int s = line.indexOf("{{") + 2;
                                int e = line.indexOf("}}");
                                String var = line.substring(s, e);
                                line.replace("{{" + var + "}}", processor(var, linkStatus));
                            }
                            client.println(line);
                        }
                    }
                    else
                    {
                        uint8_t buf[512];
                        while (file.available())
                        {
                            int n = file.read(buf, sizeof(buf));
                            client.write(buf, n);
                        }
                    }
                    file.close();
                }
                else
                {
                    Serial.print("ERROR 404: ");
                    Serial.println(path);
                    client.println("HTTP/1.1 404 Not Found");
                    client.println("Content-Type: text/plain");
                    client.println();
                    client.println("404: File Not Found");
                }
            }
        }
        delay(2);
        client.stop();
    }
}