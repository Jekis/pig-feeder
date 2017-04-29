#include <avr/wdt.h>
#include <MsTimer2.h>
#include <Bounce2.h>

// Farm config
const int PIGS_TOTAL = 4;
const int g_onePigPortionRotations = 5;             // Times motor must spin around to serve one pig

// Schedule config
unsigned long g_feedOnceAt = 1000l * 60;           // Once in a 15 sec
unsigned long g_scheduleStartDelay = 1000l * 10;   // Schedule feeding will be started with delay
bool g_feedingScheduleEnabled = true;               // Schedule feeding is enabled by defult.

// Devices
const int BTN_SCHEDULE = 2;   // Controls schedule. Enables and disables it.
const int BTN_FEED = 4;       // Forces to feed now.
const int MOTOR_FEED = 3;     // Motor that feeds (Note! Pin must support PWM)
const int BUZZER = 7;     // Motor that feeds (Note! Pin must support PWM)

// Devices config
const int motorSpeedRPM = 220;              // How fast (RPM) the motor should work.

// MOS config
const float powerSupply = 9.23;             // Voltage of the power supply
const int stableMinValue = 5;               // Don't change! MOS starts working stable on this value
const int stableMaxValue = 200;             // Don't change! MOS stops working stable after this value
const float minStableValueVoltage = 1.24;   // Output voltage on the min stable value
const float maxStableValueVoltage = 8.25;   // Output voltage on the max stable value

// System varables
const unsigned long TIMER_INTERUPT_INTERVAL = 2 / 2; // In milliseconds. NOTE! Expected time must be devided by 2. (I don't know why!)
Bounce btnScheduleDebouncer = Bounce();
Bounce btnFeedDebouncer = Bounce();
unsigned long g_onePigPortionWorkTime;    // How long motor must work to feed one pig.
unsigned long g_lastFeedStartedAt;
unsigned long g_lastFeedStoppedAt = 0;
bool g_isFeedingNow = false;              // Feeding is in process.
bool g_forceFeedNow = false;              // Feed pigs bypassin schedule.
bool g_isFirstLoop = true;                // This flag is useful to know if it's a first loop or not.

bool g_doRemainingTime = false;             // Flag, when to informate about remaining time.
unsigned long g_intervalCountRamainingTime = 0;

/**
  Function prototypes.
*/
int motor(int pin, char* command = "state", float speed = 0.0); // Setup default arguments.

void setup() {
  pinMode(BTN_SCHEDULE, INPUT_PULLUP);
  pinMode(BTN_FEED, INPUT_PULLUP);
  pinMode(MOTOR_FEED, OUTPUT);

  // Enable watchdog. Set max allowed timeout for 500 ms.
  wdt_enable(WDTO_500MS);

  MsTimer2::set(TIMER_INTERUPT_INTERVAL, timerInterupt);  // Setup interupt interval
  MsTimer2::start();                                      // Allow to interupt

  Serial.begin(9600);

  btnScheduleDebouncer.attach(BTN_SCHEDULE);
  btnScheduleDebouncer.interval(5);

  btnFeedDebouncer.attach(BTN_FEED);
  btnFeedDebouncer.interval(5);

  g_onePigPortionWorkTime = motorConvertRotationsToTime(g_onePigPortionRotations, motorSpeedRPM);

  farmSetup();
}

void farmSetup()
{
  info("farm: ", "setup");

  // There is a case when motor works during the boot process.
  // Stop it explicitly.
  motor(MOTOR_FEED, "stop");
  noTone(BUZZER);

  tone(BUZZER, 1000, 500);
  info("farm: ", "started");
}

void loop() {
  unsigned long now = millis();
  bool isScheduleFeedingDelay = now < g_scheduleStartDelay

  handleBtns();

  // Start schedule feeding after delay.
  if (isScheduleFeedingDelay) {
    scheduleFeeding();
  }

  if (g_doRemainingTime) {
    // Only when motor is not working.
    if (g_feedingScheduleEnabled && !isScheduleFeedingDelay && !g_isFeedingNow) {
      float timeRemaining = (g_feedOnceAt - (now - g_lastFeedStartedAt)) / 1000;
      echo(String("Motor will start in (sec):") + String(timeRemaining, 0));
    }

    // Mark as handled
    g_doRemainingTime = false;
  }

  // First loop finished.
  if (g_isFirstLoop) {
    g_isFirstLoop = false;
  }

  // When this reset will not be reached (due to system hang up), Arduino will be rebooted.
  wdt_reset();
  delay(10);
}

/**
  Interupt handler. Will be callsed each 1 ms.
*/
void timerInterupt()
{
  static unsigned long intervalRemainingTime = 1000 * 1; // 10 seconds

  // Increase counters
  g_intervalCountRamainingTime += TIMER_INTERUPT_INTERVAL * 2; // Becase it was devided by 2 previosly.

  // Compare counters with required intervals
  if (g_intervalCountRamainingTime >= intervalRemainingTime) {
    g_intervalCountRamainingTime = 0;
    g_doRemainingTime = true;
  }
}

/**
  Handle all buttons.
*/
void handleBtns() {
  bool btnFeedClicked = btnFeedDebouncer.update() && btnFeedDebouncer.read() == LOW;
  bool btnScheduleClicked = btnScheduleDebouncer.update() && btnScheduleDebouncer.read() == LOW;

  if (btnFeedClicked) {
    feedNow();
  }

  // Enables or disables schedule feeding
  if (btnScheduleClicked) {
    g_feedingScheduleEnabled = !g_feedingScheduleEnabled;
    info("Schedule feeding: ", g_feedingScheduleEnabled ? "enabled" : "disabled");
  }

  if (g_isFirstLoop) {
    info("Schedule feeding: ", g_feedingScheduleEnabled ? "enabled" : "disabled");
  }
}

/**
   Set global variables to perform feeding right now.
*/
void feedNow()
{
    // Force feed, only if there is no feeding now.
    if (!g_isFeedingNow) {
      g_forceFeedNow = true;
      info("btn: ", "Feed now!");
    } else {
      info("btn: ", "Cannot feed right now. Try later.");
    }
}

/**
   Responses for schedule feeding.
   Feeds pigs by schedule.
*/
void scheduleFeeding() {
  unsigned long now = millis();
  bool lastFeedNotFinished = g_lastFeedStartedAt && g_lastFeedStartedAt >= g_lastFeedStoppedAt;
  bool isFeedingTime = now - g_lastFeedStoppedAt >= g_feedOnceAt;
  bool neverStartedBefore = g_lastFeedStartedAt == NULL;
  // Feeding process shoud be continued.
  bool continueFeeding = g_feedingScheduleEnabled && isFeedingTime || lastFeedNotFinished || g_forceFeedNow || neverStartedBefore;

  if (continueFeeding) {
    bool finished = feeding(PIGS_TOTAL);

    if (finished) {
      info("Waiting", "...");

      if (g_forceFeedNow) {
        g_forceFeedNow = false;
      }
    }
  }
}

/**
   Feeds pigs.
   Starts the motor and stops it after required working time is done.

   @param pigs How many pigs must be feed.
   @return Were feed or not.
*/
bool feeding(int pigs) {
  unsigned long now = millis();
  // How long motor must work to feed all pigs
  unsigned long allPigsPortionWorkTime = g_onePigPortionWorkTime * pigs;
  //allPigsPortionWorkTime = 30000;

  // Motor is off. Start it.
  if (motor(MOTOR_FEED, "state") == LOW) {
    motor(MOTOR_FEED, "start", motorSpeedRPM);
    g_lastFeedStartedAt = now;
    g_isFeedingNow = true;
  } else if (now - g_lastFeedStartedAt >= allPigsPortionWorkTime) {
    // Feed time is out. Stop the motor.
    motor(MOTOR_FEED, "stop");
    g_lastFeedStoppedAt = now;
    g_isFeedingNow = false;

    // Were feed.
    return true;
  }

  return false;
}

/**
   TODO: Remove it and use echo()
   Displays information
*/
void info(char* name, char* value) {
  Serial.print(name);
  Serial.println(value);
}

/**
   Displays information
*/
void echo(String text)
{
  Serial.println(text);
}


/**
   Motor control

   @param command Name of the command you want to execute.
   @param speed   Speed in RPMs to run motor at.
   @return motor state: on or off.
*/
int motor(int pin, char* command, float speed) {
  static int motorState = LOW;

  if (command == "start") {
    // What voltage shoud be used to have this speed?
    float voltage = motorConvertSpeedToVoltage(speed);
    voltage = 4.0;
    // What value shoud be set to have this voltage?
    float analogSpeedValue = calcPWMValue(voltage);

    analogWrite(pin, analogSpeedValue);
    motorState = HIGH;

    info("Motor: ", "started");
  } else if (command == "stop") {
    analogWrite(pin, 0);
    motorState = LOW;

    info("Motor: ", "stopped");
  }

  return motorState;
}

/**
   Converts voltage to PWM value.
   This value could be passed to the MOS sig

   @param voltage Output voltage
   @reurn required value to set in range of 0-255
*/
int calcPWMValue(float voltage) {
  static int minValue = 0;
  static int maxValue = 255;
  int value;

  if (voltage <= 0) {
    value = minValue;
  } else if (voltage < minStableValueVoltage) {
    // Return something
    value = stableMinValue / 2; // Don't care about this.
  } else if (voltage <= maxStableValueVoltage) {
    // f(x) stable
    value = (voltage - minStableValueVoltage) / (maxStableValueVoltage - minStableValueVoltage) * (stableMaxValue - stableMinValue) + stableMinValue;
  } else {
    // f(x) inaccurate
    value = (voltage - maxStableValueVoltage) / (powerSupply - maxStableValueVoltage) * (255 - stableMaxValue) + stableMaxValue;
  }

  if (value > maxValue) {
    value = maxValue;
  }

  return int(value);
}

/**
   Converts rotations to milliseconds.

   @param rotations How many rorations must be done
   @param speed     At what speed
   @return  required time.
*/
long motorConvertRotationsToTime(int rotations, int speed)
{
  unsigned long oneMinute = 1000 * 60l;
  float result = rotations * (oneMinute / speed);

  return long(result);
}

/**
   Calculates voltage that motor requires to work at a certain speed.

   This function is motor specific and was found empirically
   through testing the dependence of the motor speed from the voltage.
   Each motor must have it's own convert function.

   @param speedRPM  At what speed motor shoud work. Min value is 30 and max value is 50
   @return Voltage required.
*/
float motorConvertSpeedToVoltage(float speedRPM) {
  static float N = 28.25;
  static float minValue = 30.f;
  static float maxValue = 50.f;

  // Make values safe for motor
  if (speedRPM < minValue) {
    speedRPM = minValue;
  } else if (speedRPM > maxValue) {
    speedRPM = maxValue;
  }

  return float(pow(M_E, speedRPM / N));
}


/**
   Calculates speed motor will work at based on voltage given.

   This function is motor specific and was found empirically
   through testing the dependence of the motor speed from the voltage.
   Each motor must have it's own convert function.

   @param voltage Output voltage. Min value is 3V and max value is 6V
   @return Motor speed in RPMs.
*/
float motorConvertVoltageToSpeed(float voltage) {
  static float N = 28.25;
  static float minValue = 3.f;
  static float maxValue = 6.f;

  // Make values safe for motor
  if (voltage < minValue) {
    voltage = minValue;
  } else if (voltage > maxValue) {
    voltage = maxValue;
  }

  return float(N * log(voltage));
}

