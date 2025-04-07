/*
To use this to upload the bio.txt file, upload this sketch and run it.  
Connect PC WIFI to ESP32_AP (password 12345678).  
Open a web browser at 192.168.4.1.
Select and upload file
*/

#include <WiFi.h>
#include "SPIFFS.h"
#include <WebServer.h>

// Create a web server on port 80
WebServer server(80);

// Function to list SPIFFS directory contents (printed to Serial)
void printDirectory(fs::FS &fs, const char * dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\n", dirname);
  File root = fs.open(dirname);
  if (!root) {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return;
  }
  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) {
        printDirectory(fs, file.name(), levels - 1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}

// Handler for the root URL: sends an HTML form for file upload
void handleRoot() {
  String html = "<html><head><title>ESP32 File Upload</title></head><body>";
  html += "<h1>Upload a File to SPIFFS</h1>";
  html += "<form method='POST' action='/upload' enctype='multipart/form-data'>";
  html += "<input type='file' name='upload'>";
  html += "<input type='submit' value='Upload'>";
  html += "</form>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

// This function handles the file upload events
void handleFileUpload() {
  HTTPUpload &upload = server.upload();
  static File file;
  
  if (upload.status == UPLOAD_FILE_START) {
    String filename = "/" + upload.filename;
    Serial.printf("Upload Start: %s\n", filename.c_str());
    // Open file for writing in SPIFFS (overwrite if it exists)
    file = SPIFFS.open(filename, FILE_WRITE);
    if (!file) {
      Serial.println("Failed to open file for writing");
    }
  } 
  else if (upload.status == UPLOAD_FILE_WRITE) {
    // Write received bytes to the file
    if (file)
      file.write(upload.buf, upload.currentSize);
  } 
  else if (upload.status == UPLOAD_FILE_END) {
    if (file) {
      file.close();
      Serial.printf("Upload End: %s, %u bytes\n", upload.filename.c_str(), upload.totalSize);
    }
    // Respond once the file is completely uploaded
    server.send(200, "text/plain", "File Uploaded Successfully");
    // Print updated SPIFFS directory to Serial Monitor
    printDirectory(SPIFFS, "/", 0);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

/*
// Let's try erasing the drive
  Serial.println("Erasing SPIFFS...");
  if (SPIFFS.format()) {
    Serial.println("SPIFFS erased successfully.");
  } else {
    Serial.println("Error erasing SPIFFS.");
  } 
*/

  // Mount SPIFFS (format if mounting fails)
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }
  Serial.println("SPIFFS mounted successfully");

  // Set up WiFi in Access Point mode
  const char* ssid = "ESP32_AP";
  const char* password = "12345678";
  WiFi.softAP(ssid, password);
  Serial.print("AP Started. Connect to WiFi network: ");
  Serial.println(ssid);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  // Set up web server routes
  server.on("/", HTTP_GET, handleRoot);
  // For file upload, we specify a POST handler and a file-upload callback
  server.on("/upload", HTTP_POST, [](){
    // This lambda is called when the upload is finished (the response is sent in handleFileUpload)
  }, handleFileUpload);

  server.begin();
  Serial.println("HTTP server started");

  // Optionally, list SPIFFS contents at startup
  printDirectory(SPIFFS, "/", 0);
}

void loop() {
  server.handleClient();
}
