// Code to implement a voice-acitvated interface to ChatGPT, 
// using ElevenLabs voice cloning and a biographical RAG layer to make the device pretend to be you!

#include <Arduino.h>
#include <driver/i2s.h>
#include <SPIFFS.h>
#include <string.h>
#include <ctype.h>

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// File path for biography database
const char *bioFilePath = "/bio.txt";  // Use "/bio.json" if using JSON


// -----------------------------------------------------------------------------
// Wi-Fi, OpenAI and ElevenLabs credentials
// -----------------------------------------------------------------------------

// You need to edit these four lines with your own credentials
#define WIFI_SSID     "YOUR WIFI SSID"
#define WIFI_PASSWORD "YOUR WIFI PASSWORD"
const char* openai_api_key  = "YOUR OPENAI API KEY";
const char* elevenlabs_api_key  = "YOUR ELEVENLABS API KEY";
const char* voiceID = "YOUR CLONED ELEVENLABS VOICE ID";

const char* openai_endpoint = "https://api.openai.com/v1/chat/completions";
const char* serverName      = "https://api.openai.com/v1/audio/speech";



// -----------------------------------------------------------------------------
// Global conversation / ChatGPT info
// -----------------------------------------------------------------------------
const int NCONV = 10; // Number of conversation parts to remember
String conversationHistory[NCONV];
int historyIndex = 0;


// REPLACE WITH A BRIEF DESCRIPTION OF YOURSELF, AND HOW YOU WANT CHATGPT TO BEHAVE. SOMETHING LIKE:
const char *characterCharacteristics = 
  "You are Fred Bloggs, a retired accountant." 
  "Provide fairly short helpful answers in a conversational tone.  You always respond explicitly to questions asked."
  "you do not repeat previous answers, but come up with something different to say. You do not make up biographical facts."
  "You are working over an audio link, so your responses are optimised for being heard rather than read."
  "You always use British English vocabulary and measurements.";

// Various models you can use here
const char *GPTModel = "gpt-3.5-turbo";
// Playing with this parameter alters how random ChatGPT's responses are
const float temperature = 0.35;

WiFiClientSecure client;
HTTPClient http;

// Amplifier pin definitions
#define I2S_SPK_PORT I2S_NUM_0
#define I2S_SPEAKER_BCLK  GPIO_NUM_8
#define I2S_SPEAKER_LRC   GPIO_NUM_7
#define I2S_SPEAKER_DIN   GPIO_NUM_9

// Microphone pin definitions
#define I2S_MIC_PORT I2S_NUM_1
#define I2S_MIC_BCLK  GPIO_NUM_6
#define I2S_MIC_LRC   GPIO_NUM_5
#define I2S_MIC_DOUT  GPIO_NUM_44

#define WAV_HEADER_SIZE 44
#define SAMPLE_RATE 16000
#define BUFFER_SIZE 1024
#define AMPLIFY_FACTOR 64
#define QUERYFILENAME "/question.wav"
#define RESPONSEFILENAME "/answer.pcm"

#define START_THRESHOLD 50
#define STOP_THRESHOLD 40
#define SILENCE_DURATION 2000
#define BASELINE_SMOOTHING_FACTOR 0.97f
#define BYTES_PER_SAMPLE 2
#define USER_LED_PIN 21
#define MIC_WARMUP_READS 100

float baselineVolume;

unsigned long silenceStartTime = 0;
bool recording = false;
File file;
uint32_t totalBytes = 0;
unsigned long lastPrintTime = 0;
bool speakerInstalled = false;
bool wifiOn = false;

// -----------------------------------------------------------------------------
// connectToWiFi()
// -----------------------------------------------------------------------------
void connectToWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(800);
    Serial.print(".");
    flashLED(1);
  }
  Serial.println("\nConnected to Wi-Fi.");
  wifiOn = true;
  client.setInsecure();
  http.setTimeout(15000);
}

// And switch it off when not needed to save power/heat
void disableWiFi(){
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  Serial.println("WiFi switched off");
  wifiOn = false;
}


// -----------------------------------------------------------------------------
// flashLED()
// -----------------------------------------------------------------------------
void flashLED(int nflash){
  for(int i=0; i<nflash; i++){
    digitalWrite(USER_LED_PIN, LOW);
    delay(100);
    digitalWrite(USER_LED_PIN, HIGH);
    delay(100);
  }
}

void setupMicrophone() {
  i2s_driver_uninstall(I2S_MIC_PORT);
  delay(100);

  i2s_config_t mic_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S_MSB,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = BUFFER_SIZE,
    .use_apll = 0,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t mic_pin_config = {
    .bck_io_num = I2S_MIC_BCLK,
    .ws_io_num = I2S_MIC_LRC,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_MIC_DOUT
  };

  i2s_driver_install(I2S_MIC_PORT, &mic_config, 0, NULL);
  i2s_set_pin(I2S_MIC_PORT, &mic_pin_config);
  i2s_set_clk(I2S_MIC_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
  Serial.println("Microphone I2S Configured.");

  // Discard initial noisy reads
  int16_t discardBuf[BUFFER_SIZE];
  size_t bytesRead;
  for (int i = 0; i < MIC_WARMUP_READS; i++) {
    i2s_read(I2S_MIC_PORT, discardBuf, sizeof(discardBuf), &bytesRead, 10);
  }
  Serial.println("Microphone warmup done.");
}

void setupSpeaker() {
  if (speakerInstalled) {
    i2s_driver_uninstall(I2S_SPK_PORT);
    delay(100);
    speakerInstalled = false;
  }

  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S_MSB,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = BUFFER_SIZE,
    .use_apll = 0,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SPEAKER_BCLK,
    .ws_io_num = I2S_SPEAKER_LRC,
    .data_out_num = I2S_SPEAKER_DIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_SPK_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_SPK_PORT, &pin_config);
  i2s_set_clk(I2S_SPK_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
  speakerInstalled = true;
  Serial.println("Speaker I2S Configured.");
}

void writeWAVHeader(File file, uint32_t dataSize) {
  uint32_t fileSize = 36 + dataSize;
  uint16_t audioFormat = 1;
  uint16_t numChannels = 1;
  uint16_t bitsPerSample = 16;
  uint32_t byteRate = SAMPLE_RATE * numChannels * bitsPerSample / 8;
  uint16_t blockAlign = numChannels * bitsPerSample / 8;
  uint32_t sampleRate = SAMPLE_RATE;

  file.seek(0);
  file.write((const uint8_t*)"RIFF", 4);
  file.write((uint8_t*)&fileSize, 4);
  file.write((const uint8_t*)"WAVE", 4);
  file.write((const uint8_t*)"fmt ", 4);
  uint32_t subchunk1Size = 16;
  file.write((uint8_t*)&subchunk1Size, 4);
  file.write((uint8_t*)&audioFormat, 2);
  file.write((uint8_t*)&numChannels, 2);
  file.write((uint8_t*)&sampleRate, 4);
  file.write((uint8_t*)&byteRate, 4);
  file.write((uint8_t*)&blockAlign, 2);
  file.write((uint8_t*)&bitsPerSample, 2);
  file.write((const uint8_t*)"data", 4);
  file.write((uint8_t*)&dataSize, 4);
}

void startRecording() {
  digitalWrite(USER_LED_PIN, HIGH);
  file = SPIFFS.open(QUERYFILENAME, "w+");
  for (int i = 0; i < 44; i++) file.write((uint8_t)0);
  totalBytes = 0;
  recording = true;
  silenceStartTime = millis();
  Serial.println("Started recording.");
}

void stopRecording() {
  writeWAVHeader(file, totalBytes);
  file.close();
  recording = false;
  Serial.println("Stopped recording.");
}


// -----------------------------------------------------------------------------
// Modified microphone check: Reads from the microphone and either (a) updates baseline
// if not recording, and if threshold is exceeded, starts recording and writes the triggering buffer,
// or (b) while recording, appends data and stops when enough silence is detected.
// -----------------------------------------------------------------------------
void checkMicrophoneAndRecord() {
    int16_t buffer[BUFFER_SIZE];
    size_t bytesRead = 0;
    int32_t sum = 0;

    esp_err_t result = i2s_read(I2S_MIC_PORT, buffer, sizeof(buffer), &bytesRead, 10);
    if (result != ESP_OK || bytesRead == 0) return;

    int samples = bytesRead / BYTES_PER_SAMPLE;
    if (samples == 0) return;

    for (int i = 0; i < samples; i++) {
        sum += abs(buffer[i]);
    }
    int average = sum / samples;

    if (!recording) {
        // Update baseline when the sound is below the trigger threshold.
        if (average < (START_THRESHOLD + baselineVolume)) {
            baselineVolume = (baselineVolume * BASELINE_SMOOTHING_FACTOR) +
                             (average * (1.0f - BASELINE_SMOOTHING_FACTOR));
        }
        // If the sound goes above the trigger level then start recording and
        // immediately write the current (triggering) buffer.
        if (average > (START_THRESHOLD + baselineVolume)) {
            startRecording();
            file.write((uint8_t*)buffer, bytesRead);
            totalBytes += bytesRead;
            Serial.println("Sound threshold exceeded: triggering pre-buffer saved.");
        }
    } else { // recording is true
        // Append the current buffer to the recording.
        file.write((uint8_t*)buffer, bytesRead);
        totalBytes += bytesRead;

        // Check for silence. If the average falls below the (stop) threshold we start
        // a timer, and if the silence lasts long enough, we end the recording.
        if (average < (STOP_THRESHOLD + baselineVolume)) {
            if (silenceStartTime == 0) {
                silenceStartTime = millis();
            } else if (millis() - silenceStartTime > SILENCE_DURATION) {
                stopRecording();
            }
        } else {
            silenceStartTime = 0;
        }
    }
}

void playAudioFile(const char* filename, float amplification) {
  // Determine the file extension
  const char* ext = strrchr(filename, '.');
  if (!ext) {
    Serial.println("Error: File extension not found!");
    return;
  }
  
  // Check file extension (case-insensitive)
  bool isWav = (strcasecmp(ext, ".wav") == 0);
  bool isPcm = (strcasecmp(ext, ".pcm") == 0);
  
  if (!isWav && !isPcm) {
    Serial.println("Error: Unsupported file extension!");
    return;
  }
  
  setupSpeaker();
  File playFile = SPIFFS.open(filename, "r");
  if (!playFile) {
    Serial.println("Error opening file!");
    return;
  }
  
  // Skip the WAV header if the file is a .wav file
  if (isWav) {
    playFile.seek(44);
  }
  
  int16_t buffer[BUFFER_SIZE];
  size_t bytesRead;
  while ((bytesRead = playFile.read((uint8_t*)buffer, sizeof(buffer))) > 0) {
    int sampleCount = bytesRead / sizeof(int16_t);
    // Apply amplification with clipping
    for (int i = 0; i < sampleCount; i++) {
      float amplifiedSample = (float)buffer[i] * amplification;
      if (amplifiedSample > 32767.0f) {
        amplifiedSample = 32767.0f;
      } else if (amplifiedSample < -32768.0f) {
        amplifiedSample = -32768.0f;
      }
      buffer[i] = (int16_t)amplifiedSample;
    }
    size_t bytesWritten;
    i2s_write(I2S_SPK_PORT, buffer, bytesRead, &bytesWritten, portMAX_DELAY);
  }
  
  playFile.close();
}

void measureAmbientNoise() {
  Serial.println("Measuring ambient noise...");
  setupMicrophone(); // sets up I2S & discards 100 reads

  // let mic settle ~2s
  delay(2000);

  unsigned long startTime     = millis();
  const unsigned long measureDuration = 1000; // 1s
  std::vector<long> volumes;

 
  while ((millis() - startTime) < measureDuration) {
    int16_t buffer[256];
    size_t bytesRead = 0;
    if (i2s_read(I2S_MIC_PORT, buffer, sizeof(buffer), &bytesRead, 100) == ESP_OK) {
      size_t samplesRead = bytesRead / sizeof(int16_t);
      if (samplesRead > 0) {
        long sumAbs = 0;
        for (size_t i = 0; i < samplesRead; i++) {
          sumAbs += abs(buffer[i]);
        }
        long avgVolume = sumAbs / samplesRead;
        volumes.push_back(avgVolume);
      }
    }
  }


  if(volumes.empty()){
    baselineVolume    = 0;
    Serial.println("No data => ");
    return;
  }

  long total = 0;
  for(auto v : volumes){
    total += v;
  }
  baselineVolume = float(total) / float(volumes.size());
  Serial.print("Ambient noise measurement complete: baseline=");
  Serial.println(baselineVolume);
  
}



// -----------------------------------------------------------------------------
// STTOpenAIAPI()
// -----------------------------------------------------------------------------
String STTOpenAIAPI(const char* filePath){
  if(WiFi.status()!=WL_CONNECTED){
    Serial.println("Wi-Fi not connected!");
    return "";
  }

  File wavFile=SPIFFS.open(filePath);
  if(!wavFile){
    Serial.println("Error: Failed to open WAV file.");
    return "";
  }
  size_t fileSize=wavFile.size();
  if(fileSize==0){
    Serial.println("Error: WAV file is empty.");
    wavFile.close();
    return "";
  }

  WiFiClientSecure sttClient;
  sttClient.setInsecure();
  if(!sttClient.connect("api.openai.com", 443)){
    Serial.println("Error: Connection to OpenAI STT failed.");
    wavFile.close();
    return "";
  }

  String boundary="----ESP32Boundary";
  sttClient.print("POST /v1/audio/transcriptions HTTP/1.1\r\n");
  sttClient.print("Host: api.openai.com\r\n");
  sttClient.print("Authorization: Bearer "+String(openai_api_key)+"\r\n");
  sttClient.print("Content-Type: multipart/form-data; boundary="+boundary+"\r\n");

  String modelPart= "--"+boundary+"\r\n"
                    "Content-Disposition: form-data; name=\"model\"\r\n\r\n"
                    "whisper-1\r\n";
  String filePartStart= "--"+boundary+"\r\n"
                        "Content-Disposition: form-data; name=\"file\"; filename=\"question.wav\"\r\n"
                        "Content-Type: audio/wav\r\n\r\n";
  String filePartEnd= "\r\n--"+boundary+"--\r\n";

  size_t contentLength=modelPart.length()+filePartStart.length()+fileSize+filePartEnd.length();
  sttClient.print("Content-Length: "+String(contentLength)+"\r\n\r\n");

  // send body
  sttClient.print(modelPart);
  sttClient.print(filePartStart);

  const size_t chunkSize=1024;
  uint8_t tempBuf[chunkSize];
  while(wavFile.available()){
    size_t rd=wavFile.read(tempBuf, chunkSize);
    sttClient.write(tempBuf, rd);
  }
  wavFile.close();
  sttClient.print(filePartEnd);

  // read headers
  while(sttClient.connected()){
    String line=sttClient.readStringUntil('\n');
    if(line=="\r") break;
  }

  // read body
  String response;
  while(sttClient.available()){
    response+=(char)sttClient.read();
  }

  DynamicJsonDocument doc(8192);
  DeserializationError error=deserializeJson(doc, response);
  if(error){
    Serial.print("JSON parsing error: ");
    Serial.println(error.c_str());
    return "";
  }

  if(doc["error"].is<JsonObject>()){
    String errMsg=doc["error"]["message"].as<String>();
    Serial.print("OpenAI Whisper error: ");
    Serial.println(errMsg);
    return "";
  }

  if(doc["text"].isNull()){
    Serial.println("STT API returned null or empty text.");
    return "";
  }

  String text=doc["text"].as<String>();
  return tidyStringForJSON(text);
}


/**
 * Helper to remove basic punctuation (e.g. . , ! ?) and lowercase the string.
 * Adjust for any additional punctuation or special characters you need to strip.
 */
String normalizeString(const String& input) {
    String result;
    for (unsigned int i = 0; i < input.length(); i++) {
        char c = input.charAt(i);
        // Keep letters, digits, space; skip punctuation
        if (isalnum(c) || isSpace(c)) {
            result += (char)tolower(c);
        }
        // else skip punctuation characters like '.' ',' '!' '?', etc.
    }
    return result;
}

String findRelevantBioFacts(String userInput) {
    File file = SPIFFS.open(bioFilePath);
    if (!file) {
        Serial.println("Error opening bio file");
        return "";
    }

    // First, normalize the entire user input once
    String normalizedUserInput = normalizeString(userInput);

    String bioFacts = "";
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();

        int colonIndex = line.indexOf(":");
        if (colonIndex > 0) {
            String keywords = line.substring(0, colonIndex);
            String fact = line.substring(colonIndex + 1);
            fact.trim();

            int keywordStart = 0;
            while (true) {
                int commaIndex = keywords.indexOf(',', keywordStart);
                if (commaIndex == -1) commaIndex = keywords.length();

                String keyword = keywords.substring(keywordStart, commaIndex);
                keyword.trim();

                // Normalize the keyword
                String normalizedKeyword = normalizeString(keyword);

                // Check if the normalized keyword appears in normalized user input
                if (normalizedUserInput.indexOf(normalizedKeyword) != -1 && normalizedKeyword.length() > 0) {
                    bioFacts += fact + " ";
                    break;  // Avoid adding the same fact multiple times
                }

                if (commaIndex == keywords.length()) break;
                keywordStart = commaIndex + 1;
            }
        }
    }
    file.close();
    return bioFacts;
}



// Function to prepend relevant biographical facts to user input
String prependBioToQuery(String userInput) {
    Serial.println("Checking biographical details...");
    String bioFacts = findRelevantBioFacts(userInput);
    if (bioFacts.length() > 0) {
        bioFacts = "Here is some biographical information that overrides any previous knowledge, or anything you might make up: "
                  + bioFacts
                  + "When you answer the question that follows, you can draw on this information, but only if it is relevant."
                  + "You must not quote directly from it, nor must you give any indication that I have just told you these facts. Here is the question: ";
    }
    printFormatted("Bio Facts: " + bioFacts + userInput, 120);
    return bioFacts + userInput;
}


// -----------------------------------------------------------------------------
// ChatGPTOpenAIAPI()
// -----------------------------------------------------------------------------
String ChatGPTOpenAIAPI(String prompt){
  if(WiFi.status()!=WL_CONNECTED){
    Serial.println("Wi-Fi not connected!");
    return "";
  }

  HTTPClient localHttp;
  localHttp.begin(openai_endpoint);
  localHttp.addHeader("Content-Type", "application/json");
  localHttp.addHeader("Authorization", String("Bearer ")+openai_api_key);

  String escapedPrompt=tidyStringForJSON(prompt);
  DynamicJsonDocument doc(8000*NCONV);
  JsonArray messages=doc.createNestedArray("messages");

  // system role
  {
    JsonObject systemMessage = messages.createNestedObject();
    systemMessage["role"]    = "system";
    systemMessage["content"] = characterCharacteristics;
  }

  // conversation history
  for(int i=0; i<NCONV; i++){
    if(conversationHistory[i]!=""){
      JsonObject userMsg=messages.createNestedObject();
      userMsg["role"]="user";
      userMsg["content"]=tidyStringForJSON(conversationHistory[i]);
    }
  }

  // current prompt
  {
    JsonObject userPrompt=messages.createNestedObject();
    userPrompt["role"]="user";
    userPrompt["content"]=escapedPrompt;
  }

  doc["model"]=GPTModel;
  doc["max_tokens"]=1000;
  doc["temperature"]=temperature;

  String payload;
  serializeJson(doc, payload);

  localHttp.setTimeout(15000);
  int httpResponseCode=localHttp.POST(payload);

  if(httpResponseCode>0){
    String response=localHttp.getString();
    StaticJsonDocument<2048> responseDoc;
    DeserializationError err=deserializeJson(responseDoc, response);
    if(err){
      Serial.print("JSON deserialization error: ");
      Serial.println(err.c_str());
      return "";
    }
    const char* content=responseDoc["choices"][0]["message"]["content"];
    if(content){
      // update conversation history
      conversationHistory[historyIndex]=prompt;
      historyIndex=(historyIndex+1)%NCONV;
      conversationHistory[historyIndex]=String(content);
      historyIndex=(historyIndex+1)%NCONV;

      doc.clear();
      doc.shrinkToFit();
      return String(content);
    }
  } else{
    Serial.print("HTTP request failed. Response code: ");
    Serial.println(httpResponseCode);
  }

  return "";
}

// -----------------------------------------------------------------------------
// tidyStringForJSON()
// -----------------------------------------------------------------------------
String tidyStringForJSON(String input) {
  String out;
  for (int i = 0; i < input.length(); ) {
    unsigned char c = input.charAt(i);
    
    // If ASCII (0x00 - 0x7F), handle special characters
    if (c < 0x80) {
      switch (c) {
        case '\"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
          // Escape any non-printable or non-standard characters
          if (c < 0x20 || c > 0x7E) {
            char buf[7];
            sprintf(buf, "\\u%04X", c);
            out += buf;
          } else {
            out += (char)c;
          }
          break;
      }
      i++;
    } else {
      // Decode multi-byte UTF-8 sequence to full Unicode code point
      int codePoint = 0;
      int additionalBytes = 0;
      if ((c & 0xE0) == 0xC0) { // 2-byte sequence
        codePoint = c & 0x1F;
        additionalBytes = 1;
      } else if ((c & 0xF0) == 0xE0) { // 3-byte sequence
        codePoint = c & 0x0F;
        additionalBytes = 2;
      } else if ((c & 0xF8) == 0xF0) { // 4-byte sequence
        codePoint = c & 0x07;
        additionalBytes = 3;
      } else {
        // Invalid UTF-8 leading byte; treat it as a single character
        codePoint = c;
        additionalBytes = 0;
      }
      // Ensure there are enough bytes remaining
      if (i + additionalBytes >= input.length()) break;
      for (int j = 1; j <= additionalBytes; j++) {
        unsigned char nextByte = input.charAt(i + j);
        // Validate that continuation byte is in the form 10xxxxxx
        if ((nextByte & 0xC0) != 0x80) {
          // If not valid, skip this byte (or handle error as needed)
          codePoint = nextByte;
          additionalBytes = j - 1;
          break;
        }
        codePoint = (codePoint << 6) | (nextByte & 0x3F);
      }
      i += additionalBytes + 1;
      
      // Replace non-standard punctuation with standard ASCII equivalents
      if (codePoint == 0x2018 || codePoint == 0x2019) { // left/right smart apostrophes
        out += '\'';
      } else if (codePoint == 0x201C || codePoint == 0x201D) { // left/right smart quotes
        out += '\"';
      } else if (codePoint == 0x2013 || codePoint == 0x2014) { // en dash / em dash
        out += '-';
      } else if (codePoint == 0x2026) { // ellipsis
        out += "...";
      } else {
        // For code points in the ASCII printable range, output directly
        if (codePoint >= 0x20 && codePoint <= 0x7E) {
          out += (char)codePoint;
        } else if (codePoint <= 0xFFFF) {
          // BMP code point: escape as a single Unicode sequence
          char buf[7];
          sprintf(buf, "\\u%04X", codePoint);
          out += buf;
        } else {
          // For code points outside the BMP, encode as surrogate pair.
          codePoint -= 0x10000;
          int highSurrogate = 0xD800 | (codePoint >> 10);
          int lowSurrogate  = 0xDC00 | (codePoint & 0x3FF);
          char buf[14];
          sprintf(buf, "\\u%04X\\u%04X", highSurrogate, lowSurrogate);
          out += buf;
        }
      }
    }
  }
  return out;
}

// ElevenLabs TTS is struggling to say some strings, so lets simplify by stripping out all non-basic ASCII characters
String stripNonAscii(String input) {
  String output;
  output.reserve(input.length());  // optional optimization

  for (unsigned int i = 0; i < input.length(); i++) {
    char c = input.charAt(i);
    // Keep only characters in the printable ASCII range (32 through 126)
    if (c >= 32 && c <= 126) {
      output += c;
    }
  }
  return output;
}


// -----------------------------------------------------------------------------
// TTSElevenLabsAPI()
// -----------------------------------------------------------------------------
bool TTSElevenLabsAPI(String text) {
  String voiceID = "PK8FeJai69lKQ0xompmO";  // Replace with your desired ElevenLabs voice ID
  String apiUrl = "https://api.elevenlabs.io/v1/text-to-speech/" + voiceID + "?output_format=pcm_16000";

  http.begin(client, apiUrl);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("xi-api-key", elevenlabs_api_key);  // Use the correct ElevenLabs API key

  String cleantext = tidyStringForJSON(text);
  String payload = "{\"text\":\"" + cleantext + "\", \"model_id\":\"eleven_multilingual_v2\"}";
  //String payload = "{\"text\":\"" + cleantext + "\", \"model_id\":\"eleven_flash_v2\"}";

  int httpResponseCode = http.POST(payload);

  if (httpResponseCode == 200) {
    File outputFile = SPIFFS.open(RESPONSEFILENAME, FILE_WRITE);
    if (!outputFile) {
      Serial.println("Failed to open file for writing.");
      http.end();
      return false;
    }
    
    http.writeToStream(&outputFile);
    outputFile.close();
    http.end();
    return true;
  } else {
    Serial.printf("HTTP POST failed with code %d\n", httpResponseCode);
    String respBody = http.getString();
    Serial.println("Response body: " + respBody);
    http.end();
    return false;
  }
}


// -----------------------------------------------------------------------------
// printFormatted()
// -----------------------------------------------------------------------------
void printFormatted(String input,int lineWidth){
  int len=input.length();
  int pos=0;
  while(pos<len){
    int nextLineBreak=input.indexOf('\n',pos);
    int nextCarriageReturn=input.indexOf('\r',pos);
    int nextBreak=-1;
    if(nextLineBreak!=-1 && nextCarriageReturn!=-1){
      nextBreak=min(nextLineBreak,nextCarriageReturn);
    } else if(nextLineBreak!=-1){
      nextBreak=nextLineBreak;
    } else if(nextCarriageReturn!=-1){
      nextBreak=nextCarriageReturn;
    }

    if(nextBreak!=-1 && nextBreak<pos+lineWidth){
      String seg=input.substring(pos,nextBreak);
      Serial.println(seg);
      pos=nextBreak+1;
    } else{
      int nextPos=pos+lineWidth;
      if(nextPos>=len){
        Serial.println(input.substring(pos));
        break;
      }
      int lastSpace=input.lastIndexOf(' ', nextPos);
      if(lastSpace<=pos){
        Serial.println(input.substring(pos,nextPos));
        pos=nextPos;
      } else{
        Serial.println(input.substring(pos,lastSpace));
        pos=lastSpace+1;
      }
    }
  }
}


void setup() {
  Serial.begin(115200);
  pinMode(USER_LED_PIN, OUTPUT);
  digitalWrite(USER_LED_PIN, HIGH);

  connectToWiFi();

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }
  Serial.println("SPIFFS mounted");

  measureAmbientNoise();


 // Quick test: greet politely
  String chatResponse=ChatGPTOpenAIAPI("Please say: Hello, is there something you'd like to talk about?");
  Serial.println("Response:");
  Serial.println(chatResponse);

  String cleanChatResponse=tidyStringForJSON(chatResponse);
  if(TTSElevenLabsAPI(cleanChatResponse)){
    playAudioFile(RESPONSEFILENAME, 6.);
  } else{
    Serial.println("Failed TTS greeting");
  }



  setupMicrophone();
  setupSpeaker();
  digitalWrite(USER_LED_PIN, LOW);
}

void loop() {

// turn off WiFi when not required to reduce power consumption:
  if (wifiOn) {
    disableWiFi();
  }
  // Instead of using the return value, simply read from the microphone which manages the recording state:
  checkMicrophoneAndRecord();

  if (!recording && SPIFFS.exists(QUERYFILENAME)) {

// Useful for debugging microphone and recordings
//    setupSpeaker();
//    playAudioFile(QUERYFILENAME, 32.);

    // Turn on WiFi when needed
    connectToWiFi();

    flashLED(1);
    String transcript = STTOpenAIAPI(QUERYFILENAME);
    Serial.println("Question ------------------------------------------");
    Serial.println(transcript);

    flashLED(2);
  // Add biographical details to try to prevent hallucinations
    String bioPlusTranscript = tidyStringForJSON(prependBioToQuery(transcript));


    flashLED(3);
    String chatResponse = ChatGPTOpenAIAPI(bioPlusTranscript);
    Serial.println("Answer --------------------------------------------");
    printFormatted(chatResponse, 120);
    Serial.println(" ");

    flashLED(4);
    // try stripping all non-basic ASCII, to see if that helps ElevenLabs with its pronunciation
    String cleanChatResponse = stripNonAscii(tidyStringForJSON(chatResponse));
    if(TTSElevenLabsAPI(cleanChatResponse)) {
      playAudioFile(RESPONSEFILENAME, 6.);
    } else {
      Serial.println("Failed to process text.");
    }

    // Blink LED
    flashLED(5);
    SPIFFS.remove(QUERYFILENAME);
    digitalWrite(USER_LED_PIN, LOW);

  }
}


