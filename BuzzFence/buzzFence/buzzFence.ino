#include <WiFi.h>

// Pin definitions
const int MOTOR_PIN = 5;        // GPIO5 - PWM capable pin for transistor base
// const int LED_BUILTIN = 13;     // Built-in LED on Nano ESP32

// Network configuration
const char* TARGET_SSID = "thatOne";

// Vibration and timing settings
const int RSSI_THRESHOLD = -75;           // dBm - boundary threshold
const int BUZZ_DURATION = 300;            // ms - each buzz length
const int BUZZ_PAUSE = 100;               // ms - pause between buzzes
const int PATTERN_PAUSE = 2000;           // ms - pause after 3 buzzes
const int NORMAL_SCAN_INTERVAL = 1000;    // ms - normal WiFi scan rate
const int FAST_SCAN_INTERVAL = 200;       // ms - fast scan when near boundary
const int FAILSAFE_TIMEOUT = 30000;       // ms - 30 seconds no WiFi = failsafe
const int MAX_SCAN_ATTEMPTS = 3;          // attempts before failsafe

// State variables
unsigned long lastScanTime = 0;
unsigned long lastNetworkSeen = 0;
unsigned long buzzStartTime = 0;
int currentScanInterval = NORMAL_SCAN_INTERVAL;
int failedScans = 0;
bool inBoundaryWarning = false;
bool inFailsafe = false;
int buzzCount = 0;
bool buzzing = false;
bool buzzPause = false;

// Motor control variables
int currentMotorSpeed = 0;

void setup() {
  Serial.begin(115200);
  
  // Initialize pins
  pinMode(MOTOR_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  
  // Start with motor off
  analogWrite(MOTOR_PIN, 0);
  digitalWrite(LED_BUILTIN, LOW);
  
  // Initialize WiFi in station mode (no connection, just scanning)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  
  Serial.println("Dog Collar Digital Fence Started");
  Serial.println("Target Network: " + String(TARGET_SSID));
  Serial.println("RSSI Threshold: " + String(RSSI_THRESHOLD) + " dBm");
  
  lastNetworkSeen = millis();
  delay(1000);
}

void loop() {
  unsigned long currentTime = millis();
  
  // Check if it's time to scan for WiFi
  if (currentTime - lastScanTime >= currentScanInterval) {
    scanAndRespond();
    lastScanTime = currentTime;
  }
  
  // Handle boundary warning pattern
  if (inBoundaryWarning && !inFailsafe) {
    handleBoundaryWarning();
  }
  
  // Handle failsafe mode
  // if (inFailsafe) {
  //   handleFailsafe();
  // }
  
  // Small delay to prevent excessive CPU usage
  delay(10);
}

void scanAndRespond() {
  Serial.println("Scanning for networks...");
  
  int networkCount = WiFi.scanNetworks();
  bool targetFound = false;
  int targetRSSI = -100; // Very weak default
  
  if (networkCount > 0) {
    for (int i = 0; i < networkCount; i++) {
      if (WiFi.SSID(i) == TARGET_SSID) {
        targetFound = true;
        targetRSSI = WiFi.RSSI(i);
        lastNetworkSeen = millis();
        failedScans = 0;
        Serial.println("Found " + String(TARGET_SSID) + " - RSSI: " + String(targetRSSI) + " dBm");
        break;
      }
    }
  }
  
  if (!targetFound) {
    failedScans++;
    Serial.println("Target network not found. Failed attempts: " + String(failedScans));
    
    // Check for failsafe condition
    if ((millis() - lastNetworkSeen > FAILSAFE_TIMEOUT) || (failedScans >= MAX_SCAN_ATTEMPTS)) {
      enterFailsafeMode();
      return;
    } else {
      // No network = maximum vibration
      setMotorSpeed(255);
      currentScanInterval = FAST_SCAN_INTERVAL;
      return;
    }
  }
  
  // Reset failsafe mode if we found the network
  if (inFailsafe) {
    exitFailsafeMode();
  }
  
  // Determine response based on RSSI
  if (targetRSSI <= RSSI_THRESHOLD) {
    // Weak signal - boundary warning
    if (!inBoundaryWarning) {
      enterBoundaryWarning();
    }
    currentScanInterval = FAST_SCAN_INTERVAL;
  } else {
    // Strong signal - safe zone
    if (inBoundaryWarning) {
      exitBoundaryWarning();
    }
    
    // Set motor speed inversely proportional to signal strength
    // Strong signal (-30 dBm) = low vibration
    // Weaker signal (approaching -75 dBm) = higher vibration
    int motorSpeed = mapRSSIToMotorSpeed(targetRSSI);
    setMotorSpeed(motorSpeed);
    currentScanInterval = NORMAL_SCAN_INTERVAL;
  }
}

int mapRSSIToMotorSpeed(int rssi) {
  // Map RSSI to motor speed (inverse relationship)
  // -30 dBm (strong) -> 0 PWM (no vibration)
  // -70 dBm (weak but not boundary) -> 100 PWM (moderate vibration)
  
  if (rssi >= -40) {
    return 0; // Very strong signal, no vibration needed
  } else if (rssi <= RSSI_THRESHOLD) {
    return 150; // Near boundary, but not in warning mode yet
  } else {
    // Linear mapping between -40 and threshold
    return map(rssi, -40, RSSI_THRESHOLD, 0, 150);
  }
}

void setMotorSpeed(int speed) {
  speed = constrain(speed, 0, 255);
  if (speed != currentMotorSpeed) {
    analogWrite(MOTOR_PIN, speed);
    digitalWrite(LED_BUILTIN, HIGH);
    currentMotorSpeed = speed;
    Serial.println("Motor speed set to: " + String(speed));
  }
}

void enterBoundaryWarning() {
  Serial.println("Entering boundary warning mode");
  inBoundaryWarning = true;
  buzzCount = 0;
  buzzing = false;
  buzzPause = false;
  setMotorSpeed(0); // Stop normal vibration during pattern
}

void exitBoundaryWarning() {
  Serial.println("Exiting boundary warning mode");
  inBoundaryWarning = false;
  buzzing = false;
  buzzPause = false;
  setMotorSpeed(0);
}

void handleBoundaryWarning() {
  unsigned long currentTime = millis();
  
  if (!buzzing && !buzzPause) {
    // Start new buzz
    buzzing = true;
    buzzStartTime = currentTime;
    setMotorSpeed(200); // Medium intensity for warning
    digitalWrite(LED_BUILTIN, HIGH);
    Serial.println("Buzz " + String(buzzCount + 1) + " started");
  }
  
  if (buzzing && (currentTime - buzzStartTime >= BUZZ_DURATION)) {
    // End current buzz
    buzzing = false;
    buzzCount++;
    setMotorSpeed(0);
    digitalWrite(LED_BUILTIN, LOW);
    
    if (buzzCount < 3) {
      // Start pause between buzzes
      buzzPause = true;
      buzzStartTime = currentTime;
    } else {
      // All 3 buzzes done, start long pause
      buzzCount = 0;
      buzzPause = true;
      buzzStartTime = currentTime;
      Serial.println("Pattern complete, starting long pause");
    }
  }
  
  if (buzzPause) {
    int pauseDuration = (buzzCount == 0) ? PATTERN_PAUSE : BUZZ_PAUSE;
    if (currentTime - buzzStartTime >= pauseDuration) {
      buzzPause = false;
    }
  }
}

void enterFailsafeMode() {
  if (!inFailsafe) {
    Serial.println("ENTERING FAILSAFE MODE - WiFi completely lost!");
    inFailsafe = true;
    exitBoundaryWarning(); // Stop any boundary warnings
    
    // Two warning pulses
    for (int i = 0; i < 2; i++) {
      setMotorSpeed(255);
      digitalWrite(LED_BUILTIN, HIGH);
      delay(500);
      setMotorSpeed(0);
      digitalWrite(LED_BUILTIN, LOW);
      delay(300);
    }
    
    // Then maximum continuous vibration
    setMotorSpeed(255);
    digitalWrite(LED_BUILTIN, HIGH);
  }
}

void exitFailsafeMode() {
  if (inFailsafe) {
    Serial.println("Exiting failsafe mode - WiFi restored");
    inFailsafe = false;
    setMotorSpeed(0);
    digitalWrite(LED_BUILTIN, LOW);
  }
}