// Flip clock driver code
// Automatically sets time on compile, supports US DST
// Requires Adafruit RTCLib to be installed

#include "RTClib.h"

// ----------- BEGIN CONFIGURABLE SECTION -----------

// Half steps needed for the motors to perform a single rotation.
// Usually 4096 for 28byj-48 steppers
#define STEPS_PER_REV 4096

// The default delay after each motor step in microseconds. 
// Adjusts the speed of the displays. Higher value = slower
#define STEPPER_DELAY 1400

// Comment out the following line to disable DST support
#define USE_DST

// Adjust this to the time in seconds it takes to compile and upload to the Arduino
// If the clock runs slow by a few seconds, increase this value, and vice versa. Value cannot be negative.
#define UPLOAD_OFFSET 5

// Number of repeated endstop reads during homing. Must be positive.
// Leave this value alone unless homing is unreliable.
#define ENDSTOP_DEBOUNCE_READS 3

// Stepper pin definitions
// 1st row - Hours
// 2nd row - Minutes (Tens)
// 3rd row - Minutes (Ones)
// If a display is turning backwards, reverse the order of the pins for that display.
const byte stepper_pins[][4] = { {7, 6, 5, 4},
                               {16, 10, 9, 8},
                               {14, 15, 18, 19} };

// Endstop pin definitions
// Steppers 1 and 2 share an endstop pin due to pin constraints on the Pro Micro.
// On boards with more pins available, each endstop should have its own pin.
// Same order as before: Hours, Minutes (Tens), Minutes (Ones)
const byte endstop_pins[] = {21,20,20};

// LED pin - flashes slowly when an error is detected, powered off otherwise. Can comment out to disable this functionality.
// On the Pro Micro, pin 30 is the TX LED.
#define LED_PIN 30
// Set this to true if a high signal turns off the LED.
#define LED_INVERT true

// Homing settings - modify these to calibrate your displays

// Set these to the number that is displayed after the endstop is triggered.
// Note: If the starting digit for the hours display is 12, enter 0 below.
const byte starting_digits[] = {0, 0, 9};

// The following parameter configures how much the stepper turns after homing.
// Starting offset should be increased until the top flap touches the front stop.
const unsigned int starting_offset[] = {110, 80, 265};

// ----------- END CONFIGURABLE SECTION -----------

// Position in steps of each stepper (relative to homing point)
unsigned int stepper_pos[] = {0,0,0};

// Current drive step for each stepper - 0 to 7 for half-stepping
byte drive_step[] = {0, 0, 0};

RTC_DS3231 rtc;

#if defined(USE_DST)
  unsigned int year_old = 0;
  DateTime dst_start, dst_end;
#endif

// Stops the program. Flashes LED slowly if LED pin is defined.
void e_stop() {
  #if defined(LED_PIN)
    while(1) {
      digitalWrite(LED_PIN, HIGH);
      delay(1000);
      digitalWrite(LED_PIN, LOW);
      delay(1000);
    }
  #else
    abort();
  #endif
}

void disable_stepper(const byte stepper_num) {
  digitalWrite(stepper_pins[stepper_num][0], LOW);
  digitalWrite(stepper_pins[stepper_num][1], LOW);
  digitalWrite(stepper_pins[stepper_num][2], LOW);
  digitalWrite(stepper_pins[stepper_num][3], LOW);
}

void half_step(const byte stepper_num) {
  const byte pos = drive_step[stepper_num];
  if(pos % 2 == 1) {
    digitalWrite(stepper_pins[stepper_num][(pos/2 + 1) % 4], HIGH);
    digitalWrite(stepper_pins[stepper_num][(pos/2 + 2) % 4], LOW);
    digitalWrite(stepper_pins[stepper_num][(pos/2 + 3) % 4], LOW);
    digitalWrite(stepper_pins[stepper_num][(pos/2) % 4], LOW);
  } else {
    digitalWrite(stepper_pins[stepper_num][(pos/2) % 4], HIGH);
    digitalWrite(stepper_pins[stepper_num][(pos/2 + 1) % 4], HIGH);
    digitalWrite(stepper_pins[stepper_num][(pos/2 + 2) % 4], LOW);
    digitalWrite(stepper_pins[stepper_num][(pos/2 + 3) % 4], LOW);
  }
  drive_step[stepper_num] = (drive_step[stepper_num] + 1) % 8;
}

// Take the specified amount of steps for a stepper connected to the specified pins, with a
// specified delay (in microseconds) between each step.
void step_num(const byte stepper_num, unsigned int steps, const unsigned int wait) {
  stepper_pos[stepper_num] = (stepper_pos[stepper_num] + steps) % STEPS_PER_REV;
  while(steps > 0) {
    half_step(stepper_num);
    steps--;  
    delayMicroseconds(wait);
  }
}

// Step until the endstop is pressed and released, and update the stepper position to 0.
// If homing doesn't end after 2 rotations, the endstop is assumed to have failed and the program aborts.
void step_to_home(const byte stepper_num, const unsigned int wait) {  
  unsigned int total_steps = 0;
  byte endstop_repeats = 0;
  
  // Step until endstop reads low ENDSTOP_DEBOUNCE_READS times in a row
  while(endstop_repeats < ENDSTOP_DEBOUNCE_READS) {
    endstop_repeats = digitalRead(endstop_pins[stepper_num]) == LOW ? endstop_repeats + 1 : 0;
    
    half_step(stepper_num);    
    total_steps++;
    if(total_steps > STEPS_PER_REV * 2u) {
      disable_stepper(stepper_num);
      e_stop();
    } 
    delayMicroseconds(wait);
  }
  endstop_repeats = 0;
  
  // Step until endstop reads high ENDSTOP_DEBOUNCE_READS times in a row
  while(endstop_repeats < ENDSTOP_DEBOUNCE_READS) {
    endstop_repeats = digitalRead(endstop_pins[stepper_num]) == HIGH ? endstop_repeats + 1 : 0;
    
    half_step(stepper_num);
    total_steps++;
    if(total_steps > STEPS_PER_REV * 2u) {
      disable_stepper(stepper_num);
      e_stop();
    }
    delayMicroseconds(wait);
  }

  stepper_pos[stepper_num] = 0; 
}

// Step to the specified position. If the current position is greater than the target position, this
// function will re-home and then step to the target position.
void step_to_position(const byte stepper_num, unsigned int target_pos, const unsigned int wait) {
  if (target_pos == stepper_pos[stepper_num]) {
    return;
  }

  // Limit target position to between 0 and STEPS_PER_REV-1
  target_pos %= STEPS_PER_REV;
  
  if (target_pos < stepper_pos[stepper_num]) {
    step_to_home(stepper_num, wait);
    step_num(stepper_num, target_pos, wait);
  } else {
    step_num(stepper_num, target_pos - stepper_pos[stepper_num], wait);
  }
}

// Steps to the specified digit on the display
// To save power, stepper is powered off after running, so exact positional accuracy is not maintained.
// This is allowable since the displays self-calibrate via the endstops every rotation.
void step_to_digit(const byte stepper_num, const byte digit, const unsigned int wait) {
  // The ones display has 10 flaps, the others have 12 flaps
  const byte num_flaps = (stepper_num == 2) ? 10 : 12;
  
  const byte num_digits = (stepper_num == 0) ? 12 : (stepper_num == 1) ? 6 : 10;

  const unsigned int target_pos = starting_offset[stepper_num] + (unsigned int)((num_digits + digit - starting_digits[stepper_num]) % num_digits) * STEPS_PER_REV/num_flaps;
  
  // The tens display has 2 full sets of digits, so we'll need to step to the closest target digit.
  if(stepper_num == 1) {
    // The repeated digit is offset by a half-rotation from the first target.
    const unsigned int second_target = target_pos + STEPS_PER_REV/2;

    // If the current position is between the two target positions, step to the second target position, as it's the closest given that we can't step backwards.
    // Otherwise, step to the first target position.
    if(stepper_pos[stepper_num] > target_pos && stepper_pos[stepper_num] <= second_target) {
      step_to_position(stepper_num, second_target, wait);
    } else {
      step_to_position(stepper_num, target_pos, wait);
    }
  } else {
    // The ones and hour displays only have a single set of digits, so simply step to the target position.
    step_to_position(stepper_num, target_pos, wait);
  }
  
  disable_stepper(stepper_num);
}

void setup () {
  // Setting stepper pins to output
  for (byte i = 0; i < 3; i++) {
    for (byte j = 0; j < 4; j++) {
      pinMode(stepper_pins[i][j], OUTPUT);
    }
  }
  
  // Set endstop pins as input with pullups enabled
  pinMode(endstop_pins[0], INPUT_PULLUP);
  pinMode(endstop_pins[1], INPUT_PULLUP);
  pinMode(endstop_pins[2], INPUT_PULLUP);

  // Set LED pin to output and power it off
  #if defined(LED_PIN)
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LED_INVERT);
  #endif

  if (!rtc.begin()) {
    e_stop();
  }

  if (rtc.lostPower()) {
    // Set the time to the compile time + offset
    DateTime compile_time = DateTime(F(__DATE__), F(__TIME__)) + TimeSpan(UPLOAD_OFFSET);
    
    #if defined(USE_DST)
      // Checking if compile time is in DST
      uint16_t year = compile_time.year();
    
      // DST starts on the second Sunday of March, 2AM
      // Get beginning of second week and then offset to Sunday
      DateTime dst_start = DateTime(year, 3, 8, 2, 0, 0);
      dst_start = dst_start + TimeSpan((7-dst_start.dayOfTheWeek()) % 7, 0, 0, 0);
    
      // DST ends on the first Sunday of November, 2AM
      // Get first day of month and then offset to Sunday
      DateTime dst_end = DateTime(year, 11, 1, 2, 0, 0);
      dst_end = dst_end + TimeSpan((7-dst_end.dayOfTheWeek()) % 7, 0, 0, 0);
    
      // If compile time is between DST start and end, then subtract 1 hour to get standard time
      compile_time = compile_time >= dst_start && compile_time < dst_end ? (compile_time - TimeSpan(0,1,0,0)) : compile_time;
    #endif
    
    rtc.adjust(compile_time);
  }
  
  // If the minute displays share an endstop pin (necessary on the Pro Micro), we need to make sure both of their endstops are unpressed before homing.
  // We can do this by alternatingly stepping each display in small increments until the endstop pin reads high.
  unsigned int total_steps = 0;
  if(endstop_pins[1] == endstop_pins[2]) {
    while(digitalRead(endstop_pins[1]) == LOW) {
      step_num(1, 200, STEPPER_DELAY);
      disable_stepper(1);

      // Still pressed, try the other display
      if(digitalRead(endstop_pins[1]) == LOW) {
        step_num(2, 200, STEPPER_DELAY);  
        disable_stepper(2);
      }

      // Similar to homing, if a max number of steps is reached, the endstop is assumed to have failed and the program aborts.
      total_steps += 200;
      if(total_steps > STEPS_PER_REV) {
        e_stop();
      }
    }
  }

  // Step to home digit
  step_to_home(2, STEPPER_DELAY);
  step_to_digit(2, starting_digits[2], STEPPER_DELAY);
  step_to_home(1, STEPPER_DELAY);
  step_to_digit(1, starting_digits[1], STEPPER_DELAY);
  step_to_home(0, STEPPER_DELAY);
  step_to_digit(0, starting_digits[0], STEPPER_DELAY);
}

void loop () {
  DateTime now = rtc.now();
  byte hr = now.hour() % 12;
  byte tens = now.minute() / 10;
  byte ones = now.minute() % 10;
  
  #if defined(USE_DST)
    // Calculate new DST cutoffs if the year changes
    if(now.year() != year_old) {
      // DST starts on the second Sunday of March, 2AM
      dst_start = DateTime(now.year(), 3, 8, 2, 0, 0);
      dst_start = dst_start + TimeSpan((7-dst_start.dayOfTheWeek()) % 7, 0, 0, 0);
    
      // DST ends on the first Sunday of November, 1AM (standard time)
      dst_end = DateTime(now.year(), 11, 1, 1, 0, 0);
      dst_end = dst_end + TimeSpan((7-dst_end.dayOfTheWeek()) % 7, 0, 0, 0); 
      
      year_old = now.year();
    }
    
    // If current time is between the DST cutoffs, add 1 to the hour digit
    hr = (now >= dst_start && now < dst_end) ? (hr + 1) % 12 : hr;
  #endif
  
  step_to_digit(2, ones, STEPPER_DELAY);
  step_to_digit(1, tens, STEPPER_DELAY);
  step_to_digit(0, hr, STEPPER_DELAY);
}
