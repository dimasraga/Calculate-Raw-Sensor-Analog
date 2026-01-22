#ifndef WEBSERVERHANDLER_H
#define WEBSERVERHANDLER_H

#include <Arduino.h>
#include <EthernetESP32.h>
#include <SPI.h>
#include <LittleFS.h> 

class WebServerHandler {
public:
    WebServerHandler(uint16_t port);
    void begin();
    
    // Fungsi utama
    void handleClient(EthernetLinkStatus linkStatus); 
    uint16_t modbusData[4] = {0, 0, 0, 0};

private:
    EthernetServer _server;
    
    // Helper untuk mendeteksi tipe file (CSS/JS/JSON/PNG, dll)
    String getContentType(String filename);
    
    // Processor template HTML ({{VAR}})
    String processor(const String& var, EthernetLinkStatus linkStatus); 
};

#endif // WEBSERVERHANDLER_H