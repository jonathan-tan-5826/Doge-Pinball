/*
 * main.c
 *
 * Created: 3/13/2017 10:32:06 PM
 * Author : Jonathan Tan
 */
#include <avr/delay.h>
#include <avr/eeprom.h>
#include <avr/io.h>
#include <stdbool.h>
#include "a2d.h"
#include "io.h"
#include "joystick.h"
#include "scheduler.h"
#include "timer.h"

/*
 * Communication
 * Variable Declaration
 */
bool isGameMode = false;
bool isPlaying = false;
bool isBallIn = false;
bool isGoal = false;
bool isGutter = false;

/*
 * Step Motor
 * Variable Declaration
 */
typedef struct _StepMotor
{
	unsigned short remainingRotations;
	unsigned short rotationCounter;
	unsigned char direction; // 0 = left, 1 = right
} StepMotor;

#define A 0x01
#define B 0x02
#define C 0x04
#define D 0x08
#define AB 0x03
#define BC 0x06
#define CD 0x0C
#define DA 0x09
#define INITIAL_ROTATION_DEGREE 50
#define REGULAR_ROTATION_DEGREE 100
#define JOYSTICK_THRESHOLD_HIGH 800
#define JOYSTICK_THRESHOLD_LOW 300
unsigned char currentRotation = 0x00;
unsigned char clockwise[8] = {A, AB, B, BC, C, CD, D, DA};
unsigned char counterclockwise[8] = {DA, D, CD, C, BC, B, AB, A};
enum StepMotor_States { Wait_StepMotor, InitialRotation_StepMotor, RotateRight_StepMotor, RotateLeft_StepMotor, ResetFromInitial_StepMotor, Reset_StepMotor };
static StepMotor stepMotor;

/*
 * IRSensor
 * Variable Declaration
 */
#define BALLIN_IRSENSOR_THRESHOLD 990
#define GOAL_IRSENSOR_THRESHOLD 970
#define GUTTER_IRSENSOR_THRESHOLD 970
enum IRSensor_States { Wait_Sensor, On_Sensor };

/*
 * Gameplay
 * Variable Declaration
 */
#define EEPROM_ADDRESS 0x00
#define THIRTY_SECONDS 150
#define FIVE_SECONDS 25
#define NUMBER_TICKS_PER_SECOND 5
#define LCD_SECONDROW_COLUMN_BEGIN 17
static char gameTimer = 0;
unsigned char CURRENT_HIGHSCORE = 0;
const unsigned char GAME_COUNTDOWN[31][32] = {
	"Time Remaining: 0 seconds...", "Time Remaining: 1 seconds...", "Time Remaining: 2 seconds...", "Time Remaining: 3 seconds...",
	"Time Remaining: 4 seconds...", "Time Remaining: 5 seconds...", "Time Remaining: 6 seconds...", "Time Remaining: 7 seconds...",
	"Time Remaining: 8 seconds...", "Time Remaining: 9 seconds...", "Time Remaining: 10 seconds...", "Time Remaining: 11 seconds...",
	"Time Remaining: 12 seconds...", "Time Remaining: 13 seconds...", "Time Remaining: 14 seconds...", "Time Remaining: 15 seconds...",
	"Time Remaining: 16 seconds...", "Time Remaining: 17 seconds...", "Time Remaining: 18 seconds...", "Time Remaining: 19 seconds...",
	"Time Remaining: 20 seconds...", "Time Remaining: 21 seconds...", "Time Remaining: 22 seconds...", "Time Remaining: 23 seconds...",
	"Time Remaining: 24 seconds...", "Time Remaining: 25 seconds...", "Time Remaining: 26 seconds...", "Time Remaining: 27 seconds...",
	"Time Remaining: 28 seconds...", "Time Remaining: 29 seconds...", "Time Remaining: 30 seconds..."
};
const unsigned char GAME_LOST[6][32] = {
	"  Game Over :(  Restarting 0...", "  Game Over :(  Restarting 1...", "  Game Over :(  Restarting 2...",
	"  Game Over :(  Restarting 3...", "  Game Over :(  Restarting 4...", "  Game Over :(  Restarting 5..."
};
enum Game_States { Wait_Game, Wait_BallIn_Game, Play_Game, NewHighScore_Game, UpdateHighScore_Game, Won_Game, Lost_Game };

/*
 * Menu
 * Variable Declaration
 */
#define EEPROM_ADDRESS 0x00
enum MenuStates { Main_Screen, ViewHighScore_Screen, ResetHighScore_Screen, DidResetHighScore_Screen, Play_Screen };
const unsigned char HIGHSCORES[31][2] = {
	"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10",
	"11", "12", "13", "14", "15", "16", "17", "18", "19", "20",
	"21", "22", "23", "24", "25", "26", "27", "28", "29", "30"
};
unsigned char highscoreDisplayString[32];

/* 
 * Communication
 * Functions
 */
void SetGameMode(bool value)
{
	if (isGameMode != value)
	{
		isGameMode = value;
	}
}

void SetPlaying(bool value)
{
	if (isPlaying != value)
	{
		isPlaying = value;
	}
}

void SetBallIn(bool value)
{
	if (isBallIn != value)
	{
		isBallIn = value;
	}
}

void SetGoal(bool value)
{
	if (isGoal != value)
	{
		isGoal = value;
	}
}

void SetGutter(bool value)
{
	if (isGutter != value)
	{
		isGutter = value;
	}
}

/*
* Step Motor Functions
*
*/
unsigned short GetNumberPhases(unsigned short degree)
{
	return (degree / 5.625) * 64;
}

void SetStepMotor_InitialRotation()
{
	stepMotor.remainingRotations = GetNumberPhases(INITIAL_ROTATION_DEGREE);
	stepMotor.rotationCounter = 0;
	stepMotor.direction = 0;
}

void SetStepMotor_RotateLeft()
{
	stepMotor.remainingRotations = GetNumberPhases(REGULAR_ROTATION_DEGREE);
	stepMotor.rotationCounter = 0;
	stepMotor.direction = 0;
}

void SetStepMotor_RotateRight()
{
	stepMotor.remainingRotations = GetNumberPhases(REGULAR_ROTATION_DEGREE);
	stepMotor.rotationCounter = 0;
	stepMotor.direction = 1;
}

void SetStepMotor_ResetFromInitial()
{
	unsigned short remainingRotations = GetNumberPhases(INITIAL_ROTATION_DEGREE) - stepMotor.remainingRotations;
	stepMotor.remainingRotations = remainingRotations;
	stepMotor.direction = 1;
	stepMotor.rotationCounter =  0;
}

void SetStepMotor_Reset()
{
	unsigned short rotationsTaken = GetNumberPhases(REGULAR_ROTATION_DEGREE) - stepMotor.remainingRotations;
	if (rotationsTaken > stepMotor.remainingRotations) // Rotate in opposite direction
	{
		stepMotor.direction = (stepMotor.direction == 0) ? 1 : 0;
		stepMotor.remainingRotations = rotationsTaken - GetNumberPhases(INITIAL_ROTATION_DEGREE);
		stepMotor.rotationCounter = 7 - stepMotor.rotationCounter;
	}
	else if (rotationsTaken < stepMotor.remainingRotations) // Keep rotating
	{
		stepMotor.remainingRotations = GetNumberPhases(INITIAL_ROTATION_DEGREE) - rotationsTaken;
	}
	else // If same, do not move position
	{
		stepMotor.remainingRotations = 0;
	}
}

void RotateStepMotorLeft()
{
	currentRotation = counterclockwise[stepMotor.rotationCounter];
	stepMotor.rotationCounter++;
	PORTB = currentRotation;
	if (stepMotor.rotationCounter >= 8)
	{
		stepMotor.rotationCounter = 0;
	}
}

void RotateStepMotorRight()
{
	currentRotation = clockwise[stepMotor.rotationCounter];
	stepMotor.rotationCounter++;
	PORTB = currentRotation;
	if (stepMotor.rotationCounter >= 8)
	{
		stepMotor.rotationCounter = 0;
	}
}

int TickFunction_StepMotor(int state)
{
	Set_A2D_Pin(5);
	_delay_ms(1);
	unsigned short joystickXValue = 0;
	
	switch (state) // State Transitions
	{
		case Wait_StepMotor:
		if (isPlaying == true)
		{
			SetStepMotor_InitialRotation();
			state = InitialRotation_StepMotor;
		}
		else
		{
			state = Wait_StepMotor;
		}
		break;
		
		case InitialRotation_StepMotor:
		joystickXValue = ADC;
		if ((joystickXValue < JOYSTICK_THRESHOLD_LOW) || (isPlaying == false)) // Reset
		{
			SetStepMotor_ResetFromInitial();
			state = ResetFromInitial_StepMotor;
		}
		else
		{
			if (stepMotor.remainingRotations > 0)
			{
				state = InitialRotation_StepMotor;
			}
			else
			{
				SetStepMotor_RotateRight();
				state = RotateRight_StepMotor;
			}
		}
		break;
		
		case RotateLeft_StepMotor:
		joystickXValue = ADC;
		if ((joystickXValue < JOYSTICK_THRESHOLD_LOW) || (isPlaying == false)) // Reset
		{
			SetStepMotor_Reset();
			state = Reset_StepMotor;
		}
		else
		{
			if (stepMotor.remainingRotations > 0)
			{
				state = RotateLeft_StepMotor;
			}
			else
			{
				SetStepMotor_RotateRight();
				state = RotateRight_StepMotor;
			}
		}
		break;
		
		case RotateRight_StepMotor:
		joystickXValue = ADC;
		if ((joystickXValue < JOYSTICK_THRESHOLD_LOW) || (isPlaying == false)) // Reset
		{
			SetStepMotor_Reset();
			state = Reset_StepMotor;
		}
		else
		{
			if (stepMotor.remainingRotations > 0)
			{
				state = RotateRight_StepMotor;
			}
			else
			{
				SetStepMotor_RotateLeft();
				state = RotateLeft_StepMotor;
			}
		}
		break;
		
		case Reset_StepMotor:
		if (stepMotor.remainingRotations > 0)
		{
			state = Reset_StepMotor;
		}
		else
		{
			state = Wait_StepMotor;
		}
		break;
		
		case ResetFromInitial_StepMotor:
		if (stepMotor.remainingRotations > 0)
		{
			state = ResetFromInitial_StepMotor;
		}
		else
		{
			state = Wait_StepMotor;
		}
		break;
		
		default:
		break;
	}
	
	switch (state) // State actions
	{
		case Wait_StepMotor:
		break;
		
		case InitialRotation_StepMotor:
		RotateStepMotorLeft();
		stepMotor.remainingRotations--;
		break;
		
		case RotateLeft_StepMotor:
		RotateStepMotorLeft();
		stepMotor.remainingRotations--;
		break;
		
		case RotateRight_StepMotor:
		RotateStepMotorRight();
		stepMotor.remainingRotations--;
		break;
		
		case Reset_StepMotor:
		if (stepMotor.direction == 0)
		{
			RotateStepMotorLeft();
		}
		else if (stepMotor.direction == 1)
		{
			RotateStepMotorRight();
		}
		stepMotor.remainingRotations--;
		break;
		
		case ResetFromInitial_StepMotor:
		RotateStepMotorRight();
		stepMotor.remainingRotations--;
		break;
		
		default:
		break;
	}	
	
	return state;
}

/*
 * IRSensor
 * Functions
 */
int TickFunction_IRSensor_BallIn(int state)
{
	Set_A2D_Pin(0);
	_delay_ms(1);
	unsigned short ballInValue = 0;
	
	switch (state) // State Transitions
	{
		case Wait_Sensor:
		if (isGameMode == true)
		{
			state = On_Sensor;
		}
		else
		{
			state = Wait_Sensor;
		}
		break;
		
		case On_Sensor:
		if (isGameMode == false)
		{
			state = Wait_Sensor;
		}
		else
		{
			state = On_Sensor;
		}
		break;
		
		default:
		state = Wait_Sensor;
		break;
	}

	switch (state) // State Actions
	{
		case Wait_Sensor:
		break;
		
		case On_Sensor:
		ballInValue = ADC;
		if (ballInValue < BALLIN_IRSENSOR_THRESHOLD)
		{
			SetBallIn(true);
		}
		else
		{
			SetBallIn(false);
		}
		default:
		break;
	}

	return state;
}

int TickFunction_IRSensor_Goal(int state)
{
	Set_A2D_Pin(1);
	_delay_ms(1);
	unsigned short goalValue = 0;
	
	switch (state) // State Transitions
	{
		case Wait_Sensor:
		if (isGameMode == true)
		{
			state = On_Sensor;
		}
		else
		{
			state = Wait_Sensor;
		}
		break;
		
		case On_Sensor:
		if (isGameMode == false)
		{
			state = Wait_Sensor;
		}
		else
		{
			state = On_Sensor;
		}
		break;
		
		default:
		state = Wait_Sensor;
		break;
	}
	
	switch (state) // State Actions
	{
		case Wait_Sensor:
		break;
		
		case On_Sensor:
		goalValue = ADC;
		if (goalValue < GOAL_IRSENSOR_THRESHOLD)
		{
			SetGoal(true);
		}
		else
		{
			SetGoal(false);
		}
		default:
		break;
	}
	
	return state;
}

int TickFunction_IRSensor_Gutter(int state)
{
	Set_A2D_Pin(7);
	_delay_ms(1);
	unsigned short gutterValue = 0;
	
	switch (state) // State Transitions
	{
		case Wait_Sensor:
		if (isGameMode == true)
		{
			state = On_Sensor;
		}
		else
		{
			state = Wait_Sensor;
		}
		break;
		
		case On_Sensor:
		if (isGameMode == false)
		{
			state = Wait_Sensor;
		}
		else
		{
			state = On_Sensor;
		}
		break;
		
		default:
		state = Wait_Sensor;
		break;
	}
	
	switch (state) // State Actions
	{
		case Wait_Sensor:
		break;
		
		case On_Sensor:
		gutterValue = ADC;
		if (gutterValue < GUTTER_IRSENSOR_THRESHOLD)
		{
			SetGutter(true);
		}
		else
		{
			SetGutter(false);
		}
		break;
		
		default:
		break;
	}
	
	return state;
}

/*
 * Gameplay
 * Functions
 */
unsigned char GetGameTimerInSeconds()
{
	return gameTimer / NUMBER_TICKS_PER_SECOND;
}

void UpdateLCD_GameCountdown()
{
	LCD_DisplayString(1, GAME_COUNTDOWN[GetGameTimerInSeconds()]);
}

void UpdateLCD_GameWon()
{
	LCD_DisplayString(1, " -- Winner!! -- Congratulations!");
}

void UpdateLCD_GameLost()
{
	LCD_DisplayString(1, GAME_LOST[GetGameTimerInSeconds()]);
}

void UpdateLCD_Wait_BallIn()
{
	LCD_DisplayString(1, "  Insert Ball   ");
}

void UpdateLCD_NewHighScore()
{
	LCD_DisplayString(1, "-NEW HIGH SCORE-Congratulations!");
}

int TickFunction_Game(int state)
{
	Set_A2D_Pin(5);
	_delay_ms(1);
	unsigned short joystickXValue = 0;
	
	switch (state) // Game Transitions
	{
		case Wait_Game:
		if (isGameMode == true)
		{
			UpdateLCD_Wait_BallIn();
			state = Wait_BallIn_Game;
		}
		else
		{
			state = Wait_Game;
		}
		break;
		
		case Wait_BallIn_Game:
		joystickXValue = ADC;
		if (joystickXValue < JOYSTICK_THRESHOLD_LOW) // Reset
		{
			LCD_ClearScreen();
			SetGameMode(false);
			state = Wait_Game;
		}
		else
		{
			if (isBallIn == true) // Ball In
			{
				SetPlaying(true);
				SetBallIn(false);
				gameTimer = THIRTY_SECONDS;
				state = Play_Game;
			}
			else
			{
				state = Wait_BallIn_Game;
			}
		}
		break;
		
		case Play_Game:
		joystickXValue = ADC;
		if (joystickXValue < JOYSTICK_THRESHOLD_LOW) // Reset
		{
			LCD_ClearScreen();
			SetPlaying(false);
			state = Wait_Game;
		}
		else
		{
			if ((isGoal == true) && (gameTimer > 0)) // Win
			{
				SetPlaying(false);
				SetGoal(false);
				if (GetGameTimerInSeconds() > CURRENT_HIGHSCORE)
				{
					CURRENT_HIGHSCORE = GetGameTimerInSeconds();
					gameTimer = FIVE_SECONDS;
					UpdateLCD_NewHighScore();
					state = NewHighScore_Game;
				}
				else
				{
					gameTimer = FIVE_SECONDS;
					UpdateLCD_GameWon();
					state = Won_Game;
				}
			}
			else if ((gameTimer <= 0) || (isGutter == true)) // Lose
			{
				SetPlaying(false);
				SetGutter(false);
				gameTimer = FIVE_SECONDS;
				UpdateLCD_GameLost();
				state = Lost_Game;
			}
			else
			{
				state = Play_Game;
			}
		}
		break;
		
		case NewHighScore_Game:
		if (gameTimer <= 0)
		{
			eeprom_write_byte(EEPROM_ADDRESS, CURRENT_HIGHSCORE);
			_delay_ms(5);
			UpdateHighScore_DisplayString();
			LCD_ClearScreen();
			SetGameMode(false);
			state = Wait_Game;
		}
		else
		{
			state = NewHighScore_Game;
		}
		break;
		
		case Won_Game:
		if (gameTimer <= 0)
		{
			LCD_ClearScreen();
			SetGameMode(false);
			state = Wait_Game;
		}
		else
		{
			state = Won_Game;
		}
		break;
		
		case Lost_Game:
		if (gameTimer <= 0)
		{
			LCD_ClearScreen();
			SetGameMode(false);
			state = Wait_Game;
		}
		else
		{
			state = Lost_Game;
		}
		break;
		
		default:
		state = Wait_Game;
		break;
	}
	
	switch (state) // Game Actions
	{
		case Wait_Game:
		break;
		
		case Wait_BallIn_Game:
		break;
		
		case Play_Game:
		if (gameTimer % NUMBER_TICKS_PER_SECOND == 0)
		{
			UpdateLCD_GameCountdown();
		}
		--gameTimer;
		break;
		
		case NewHighScore_Game:
		if (gameTimer % NUMBER_TICKS_PER_SECOND == 0)
		{
			LCD_ClearScreen();
			_delay_ms(2);
			UpdateLCD_NewHighScore();
		}
		--gameTimer;
		break;
		
		case Won_Game:
		--gameTimer;
		break;
		
		case Lost_Game:
		if (gameTimer % NUMBER_TICKS_PER_SECOND == 0)
		{
			UpdateLCD_GameLost();
		}
		--gameTimer;
		break;
		
		default:
		break;
	}
	
	return state;
}

/*
 * Menu
 * Functions
 */
void UpdateLCD_To_MainScreen()
{
	LCD_DisplayString(1, "   -- Main --     --> To Start  ");
}

void UpdateLCD_To_ViewHighScoreScreen()
{
	LCD_DisplayString(1, highscoreDisplayString);
}

void UpdateLCD_To_ResetHighScoreScreen()
{
	LCD_DisplayString(1, "Reset High Score  --> To Reset  ");
}

void UpdateLCD_To_DidResetHighScoreScreen()
{
	LCD_DisplayString(1, "High Score Reset  <-- To Return ");
}

void SetInitialHighScore_DisplayString()
{
	unsigned char bestTime = eeprom_read_byte(EEPROM_ADDRESS);
	unsigned char test[3];
	itoa(bestTime, test, 10);
	_delay_ms(5);
	
	strcpy(highscoreDisplayString, "-- HIGH SCORE -- Best Time: ");
	strcat(highscoreDisplayString, test);
	strcat(highscoreDisplayString, "s");
}

void UpdateHighScore_DisplayString()
{
	unsigned char score[3];
	itoa(CURRENT_HIGHSCORE, score, 10);
	
	strcpy(highscoreDisplayString, "-- HIGH SCORE -- Best Time: ");
	strcat(highscoreDisplayString, score);
	strcat(highscoreDisplayString, "s");
}

void Reset_SavedHighScore()
{
	CURRENT_HIGHSCORE = 0;
	eeprom_write_byte(EEPROM_ADDRESS, CURRENT_HIGHSCORE);
	_delay_ms(5);
	
	UpdateHighScore_DisplayString();
}

int TickFunction_Menu(int state)
{
	unsigned short joystickValue_X = GetJoystick_X();
	unsigned short joystickValue_Y = GetJoystick_Y();
	
	switch(state) // state Transitions
	{
		case Main_Screen:
		if (joystickValue_X > JOYSTICK_THRESHOLD_HIGH) // Toggle Right
		{
			SetGameMode(true);
			state = Play_Screen;
		}
		else if (joystickValue_Y > JOYSTICK_THRESHOLD_HIGH) // Toggle Up
		{
			UpdateLCD_To_ViewHighScoreScreen();
			state = ViewHighScore_Screen;
		}
		else if (joystickValue_Y < JOYSTICK_THRESHOLD_LOW) // Toggle Down
		{
			UpdateLCD_To_ResetHighScoreScreen();
			state = ResetHighScore_Screen;
		}
		else
		{
			state = Main_Screen;
		}
		break;
		
		case ViewHighScore_Screen:
		if (joystickValue_Y > JOYSTICK_THRESHOLD_HIGH) // Toggle Up
		{
			UpdateLCD_To_ResetHighScoreScreen();
			state = ResetHighScore_Screen;
		}
		else if (joystickValue_Y < JOYSTICK_THRESHOLD_LOW) // Toggle Down
		{
			UpdateLCD_To_MainScreen();
			state = Main_Screen;
		}
		else
		{
			state = ViewHighScore_Screen;
		}
		break;
		
		case ResetHighScore_Screen:
		if (joystickValue_X > JOYSTICK_THRESHOLD_HIGH) // Toggle Right
		{
			state = DidResetHighScore_Screen;
			Reset_SavedHighScore();
			
			UpdateLCD_To_DidResetHighScoreScreen();
			break;
		}
		
		if (joystickValue_Y > JOYSTICK_THRESHOLD_HIGH) // Toggle Up
		{
			UpdateLCD_To_MainScreen();
			state = Main_Screen;
		}
		else if (joystickValue_Y < JOYSTICK_THRESHOLD_LOW) // Toggle Down
		{
			UpdateLCD_To_ViewHighScoreScreen();
			state = ViewHighScore_Screen;
		}
		else
		{
			state = ResetHighScore_Screen;
		}
		break;
		
		case DidResetHighScore_Screen:
		if (joystickValue_X < JOYSTICK_THRESHOLD_LOW) // Toggle Left
		{
			UpdateLCD_To_ResetHighScoreScreen();
			state = ResetHighScore_Screen;
		}
		else
		{
			state = DidResetHighScore_Screen;
		}
		break;
		
		case Play_Screen:
		if (!isGameMode)
		{
			UpdateLCD_To_MainScreen();
			state = Main_Screen;
		}
		else
		{
			state = Play_Screen;
		}
		break;
		
		default:
		joystickValue_X = joystickValue_Y = 540;
		state = Main_Screen;
		break;
	}
	
	return state;
}

int main(void)
{
	DDRA = 0x00; PORTA = 0xFF; // Input
	DDRB = 0xFF; PORTB = 0x00; // Step Motor Output
	DDRC = 0xFF; PORTC = 0x00; // LCD data lines
	DDRD = 0xFF; PORTD = 0x00; // LCD control lines
	
	// Initializations
	isGameMode = false;
	isPlaying = false;
	isBallIn = false;
	isGoal = false;
	isGutter = false;
	
	// Period for the tasks
	unsigned long int MainMenu_Calc = 100;
	unsigned long int GamePlay_Calc = 200;
	unsigned long int IRSensor_Calc = 10;
	unsigned long int StepMotor_Calc = 2;
	
	//Calculating GCD
	unsigned long int tmpGCD = 1;
	tmpGCD = findGCD(IRSensor_Calc, GamePlay_Calc);
	tmpGCD = findGCD(tmpGCD, StepMotor_Calc);
	tmpGCD = findGCD(tmpGCD, MainMenu_Calc);

	//Greatest common divisor for all tasks or smallest time unit for tasks.
	unsigned long int GCD = tmpGCD;

	//Recalculate GCD periods for scheduler
	unsigned long int GamePlay_Period = GamePlay_Calc/GCD;
	unsigned long int IRSensor_Period = IRSensor_Calc/GCD;
	unsigned long int StepMotor_Period = StepMotor_Calc/GCD;
	unsigned long int MainMenu_Period = MainMenu_Calc/GCD;
	
	static task MainMenu_Task, GamePlay_Task, IRSensorBallIn_Task, IRSensorGoal_Task, IRSensorGutter_Task, StepMotor_Task;
	task *tasks[] = { &MainMenu_Task, &IRSensorBallIn_Task, &IRSensorGoal_Task, &IRSensorGutter_Task, &GamePlay_Task, &StepMotor_Task };
	const unsigned short numTasks = sizeof(tasks)/sizeof(task*);
	
	MainMenu_Task.state = Main_Screen;
	MainMenu_Task.period = MainMenu_Period;
	MainMenu_Task.elapsedTime = MainMenu_Period;
	MainMenu_Task.TickFct = &TickFunction_Menu;
	
	GamePlay_Task.state = Wait_Game;
	GamePlay_Task.period = GamePlay_Period;
	GamePlay_Task.elapsedTime = GamePlay_Period;
	GamePlay_Task.TickFct = &TickFunction_Game;
	
	StepMotor_Task.state = Wait_StepMotor;
	StepMotor_Task.period = StepMotor_Period;
	StepMotor_Task.elapsedTime = StepMotor_Period;
	StepMotor_Task.TickFct = &TickFunction_StepMotor;
	
	IRSensorBallIn_Task.state = Wait_Sensor;
	IRSensorBallIn_Task.period = IRSensor_Period;
	IRSensorBallIn_Task.elapsedTime = IRSensor_Period;
	IRSensorBallIn_Task.TickFct = &TickFunction_IRSensor_BallIn;	
	
	IRSensorGoal_Task.state = Wait_Sensor;
	IRSensorGoal_Task.period = IRSensor_Period;
	IRSensorGoal_Task.elapsedTime = IRSensor_Period;
	IRSensorGoal_Task.TickFct = &TickFunction_IRSensor_Goal;
	
	IRSensorGutter_Task.state = Wait_Sensor;
	IRSensorGutter_Task.period = IRSensor_Period;
	IRSensorGutter_Task.elapsedTime = IRSensor_Period;
	IRSensorGutter_Task.TickFct = &TickFunction_IRSensor_Gutter;

	TimerSet(GCD);
	TimerOn();
	A2D_init();
	LCD_init();
	UpdateLCD_To_MainScreen();
	SetInitialHighScore_DisplayString();
	
	unsigned short i;
	while(1)
	{
		for (i = 0; i < numTasks; ++i)
		{
			if ( tasks[i]->elapsedTime == tasks[i]->period )
			{
				tasks[i]->state = tasks[i]->TickFct(tasks[i]->state);
				tasks[i]->elapsedTime = 0;
			}
			tasks[i]->elapsedTime += 1;
		}
		while(!TimerFlag);
		TimerFlag = 0;
	}
	return 0;
}
