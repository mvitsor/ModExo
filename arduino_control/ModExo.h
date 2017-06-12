//**************************************************************************
//------- Laboratorio de Biomecatronica @ Universidade de Sao Paulo --------
//
// Version: 1.0
// Date: 15.06.2017 [mm.dd.yyyy]
// Description
//  CAN Communication between Arduino and EPOS2 for Exoskeleton Control
// Input files:
// (None)
//
// Final Output files:
//   (None)
//
// References
//  http://wiki.seeed.cc/CAN-BUS_Shield_V1.2/
//  http://www.instructables.com/id/Arduino-Timer-Interrupts/
//  http://forum.arduino.cc/index.php?topic=225789.0
//  
//**************************************************************************
// Steps
//**************************************************************************

#include <SPI.h>
#include "mcp_can.h"
#include <mcp_can_dfs.h>

// Define States
#define Startup             1
#define Pre_Operational     2
#define Operational         3

// Statemachine State variable and initial value
byte State = Startup;

// the cs pin of the version after v1.1 is default to D9
// v0.9b and v1.0 is default D10
const int SPI_CS_PIN = 9;
// Set CS pin
MCP_CAN CAN(SPI_CS_PIN); 

// Sensor variables
uint32_t encoder_data = 0;
uint32_t current_data = 0;
uint32_t actualposition_data = 0;
int32_t loadcell_data = 0;
double loadcell_data_double = 0;

// ***************************
// EPOS2 CANOpen Communication
// ***************************
//
// Data comes with Lowest Bit First

// Statemachine
unsigned char set_pre_operational[2] = {0x80, 0};
unsigned char set_operational[2] = {0x1, 0};
unsigned char enable_epos[8] = {0x2B, 0x40, 0x60, 0, 0x0F, 0, 0, 0};
unsigned char disable_epos[8] = {0x2B, 0x40, 0x60, 0, 0x06, 0, 0, 0};

// PDO Configuration
unsigned char pdo_sync[1] = {0x00};

unsigned char pdo_actual_position_1[8] = {0x22, 0x02, 0x18, 0x01, 0x81, 0x03, 0, 0};
unsigned char pdo_actual_position_2[8] = {0x22, 0x02, 0x18, 0x02, 0x01, 0, 0, 0};

unsigned char pdo_actual_velocity_1[8] = {0x22, 0x02, 0x18, 0x01, 0x81, 0x03, 0, 0};
unsigned char pdo_actual_velocity_2[8] = {0x22, 0x02, 0x18, 0x02, 0x01, 0, 0, 0};

unsigned char pdo_actual_current_1[8] = {0x22, 0x00, 0x1A, 0x00, 0x00, 0, 0, 0};
unsigned char pdo_actual_current_2[8] = {0x22, 0x00, 0x1A, 0x01, 0x10, 0, 0x78, 0x60};
unsigned char pdo_actual_current_3[8] = {0x22, 0x00, 0x1A, 0x02, 0x10, 0, 0x27, 0x20};
unsigned char pdo_actual_current_4[8] = {0x22, 0x00, 0x18, 0x02, 0x01, 0, 0, 0};
unsigned char pdo_actual_current_5[8] = {0x22, 0x00, 0x1A, 0x00, 0x01, 0, 0, 0};

// *****************
// Object Writing
// *****************

// Parameters
unsigned char set_max_following_error[8] = {0x22, 0x65, 0x60, 0, 0xD0, 7, 0, 0};
unsigned char set_max_acceleration[8] = {0x22, 0xC5, 0x60, 0, 0x88, 0x13, 0, 0};
unsigned char set_max_profile_velocity[8] = {0x22, 0x7F, 0x60, 0, 0xD0, 7, 0, 0};
unsigned char set_min_position_limit[8] = {0x22, 0x7D, 0x60, 0x01, 0x20, 0x6C, 0xFB, 0xFF}; // -300000qc = 0xFFFB6C20
unsigned char set_max_position_limit[8] = {0x22, 0x7D, 0x60, 0x02, 0xE0, 0x93, 0x04, 0}; // 300000qc = 0x0493E0

// Commanding
unsigned char setpoint_position[8] = {0x22, 0x62, 0x20, 0, 0, 0, 0, 0};

// *****************
// Object Reading
// *****************

// Actual Values
unsigned char get_actual_position[8] = {0x40, 0x64, 0x60, 0, 0, 0, 0, 0};
unsigned char get_actual_velocity[8] = {0x40, 0x6C, 0x60, 0, 0, 0, 0, 0};
unsigned char get_actual_current[8] = {0x40, 0x78, 0x60, 0, 0, 0, 0, 0};

// *****************
// System Variables
// *****************

int encoder_resolution = 2000; //2000 counts per turn
int reduction = 100; // harmonic drive reduction

int CT_K = 0; // generic constant to be tweaked during runtime

void setupModExo() 
{
  Serial.begin(115200);

  while (CAN_OK != CAN.begin(CAN_1000KBPS))              // init can bus : baudrate = 500k
  {
    Serial.println("CAN BUS Shield init fail");
    Serial.println(" Init CAN BUS Shield again");
    delay(100);
  }

  Serial.println("CAN BUS Shield init ok!");

 // TIMER SETUP- the timer interrupt allows precise timed measurements of the reed switch
 //for mor info about configuration of arduino timers see http://arduino.cc/playground/Code/Timer1

 cli();//stop interrupts

 //set timer1 interrupt at 1kHz
 TCCR1A = 0;// set entire TCCR1A register to 0
 TCCR1B = 0;// same for TCCR1B\
 TCNT1  = 0;//initialize counter value to 0
 // set timer count for 1khz increments

 // OCR1A 1999 to 1kHZ, 19999 to 0.1kHz
 OCR1A = 19999;// = (16*10^6) / (1000*8) - 1
 //had to use 16 bit timer1 for this bc 1999>255, but could switch to timers 0 or 2 with larger prescaler
 // turn on CTC mode
 TCCR1B |= (1 << WGM12);
 // Set CS11 bit for 8 prescaler
 TCCR1B |= (1 << CS11);
 // enable timer compare interrupt
 TIMSK1 |= (1 << OCIE1A);

 sei();//allow interrupts
 //END TIMER SETUP
  
  Serial.println("Interrupt init ok!");
}

//***************
// CAN STARTUP
//***************
void doStartup(void) 
{
  CAN.sendMsgBuf(0x601, 0, 8, disable_epos);
  delay(10);
  CAN.sendMsgBuf(0x601, 0, 8, enable_epos);
  Serial.println("EPOS enabled");
  delay(10);
  CAN.sendMsgBuf(0x601, 0, 8, set_max_following_error);
  Serial.println("Max Following Error set as 2000qc");
  delay(10);
  CAN.sendMsgBuf(0x601, 0, 8, set_max_acceleration);
  Serial.println("Max Aceeleration set as 5000rpm/s");
  delay(10);
  CAN.sendMsgBuf(0x601, 0, 8, set_max_profile_velocity);
  Serial.println("Max Profile Velocity set as 2000rpm");  
  delay(10);
  CAN.sendMsgBuf(0x601, 0, 8, set_min_position_limit);
  Serial.println("Min Position Limit defined as -300000qc");  
  delay(10);
  CAN.sendMsgBuf(0x601, 0, 8, set_max_position_limit);
  Serial.println("Min Position Limit defined as 300000qc");  
  delay(10);  
  State = Pre_Operational;
}

//***************
// PDO Config
//***************
void PDOConfig(void) {

  CAN.sendMsgBuf(0x00, 0, 2, set_pre_operational);
  delay(10);
  CAN.sendMsgBuf(0x601, 0, 8, pdo_actual_position_1);
  delay(10);
  CAN.sendMsgBuf(0x601, 0, 8, pdo_actual_position_2);
  delay(10);
  // CAN.sendMsgBuf(0x601, 0, 8, pdo_actual_velocity_1);
  // delay(10);
  // CAN.sendMsgBuf(0x601, 0, 8, pdo_actual_velocity_2);
  delay(10);
  CAN.sendMsgBuf(0x601, 0, 8, pdo_actual_current_1);
  delay(10);
  CAN.sendMsgBuf(0x601, 0, 8, pdo_actual_current_2);
  delay(10);
  CAN.sendMsgBuf(0x601, 0, 8, pdo_actual_current_3);
  delay(10);
  CAN.sendMsgBuf(0x601, 0, 8, pdo_actual_current_4);
  delay(10);
  CAN.sendMsgBuf(0x601, 0, 8, pdo_actual_current_5);
  delay(10);
  CAN.sendMsgBuf(0x00, 0, 2, set_operational);
  delay(100);

  Serial.println("TPDO Configured!");
  
  delay(10);
  State = Operational;
}

//******
// SYNC
//******
void sync(void) {
  CAN.sendMsgBuf(0x80, 0, 1, pdo_sync);
}

int tests = 0;
bool sync_flag = 0; 

//******
// INTERRUPTION
//******
ISR(TIMER1_COMPA_vect) { 
 
  sync_flag = 1;

}

//*******************
// POSITION SETPOINT
//*******************
void positionSetpoint(uint32_t angle) 
{
  angle *= 200000 / 4096; //conversion to quadrature counts

  setpoint_position[4] = angle & 0xFF; 
  setpoint_position[5] = (angle >> 8) & 0xFF;
  setpoint_position[6] = (angle >> 16) & 0xFF;
  setpoint_position[7] = (angle >> 24) & 0xFF;

  // clear the string:
  CAN.sendMsgBuf(0x601, 0, 8, setpoint_position);
}

//******************************
// AMPLIFICATION BOARD DATA READ
//******************************
float amplificationBoardDataRead()
{
  unsigned char len = 0;
  unsigned char buf[8];

  if (CAN_MSGAVAIL == CAN.checkReceive())           // check if data coming
  {
    CAN.readMsgBuf(&len, buf);    // read data,  len: data length, buf: data buf

    unsigned int canId = CAN.getCanId();

    // encoder information is read from buf[4] and buf[5] and converted to decimal
    encoder_data = buf[4];
    encoder_data <<= 8;
    encoder_data = encoder_data | buf[5];

    // // load cell information is read from buf[1], buf[2] and buf[3] and converted to decimal
    loadcell_data = buf[1];
    loadcell_data <<= 8;
    loadcell_data = loadcell_data | buf[2];
    loadcell_data <<= 8;
    loadcell_data = loadcell_data | buf[3];
    //
    //    if (buf[1] >= 128) {
    //           loadcell_data = loadcell_data - 16777216;
    //    }

    loadcell_data_double = loadcell_data;
    //  loadcell_data_double = loadcell_data_double-4294000000;

    //    Serial.print("Encoder: ");
    //    Serial.println(encoder_data, DEC);

     Serial.print("Loadcell: ");
     Serial.println(loadcell_data_double);

    return(encoder_data);
  }
}

// //******************************
// // POSITION DATA READ
// //******************************
// float encoderDataRead(unsigned char buf[])
// {
//     unsigned char encoder_data[8];

//     encoder_data = buf[4];
//     encoder_data <<= 8;
//     encoder_data = encoder_data | buf[5];

//     Serial.print("Encoder: ");
//     Serial.println(encoder_data);
// }

//****************
// CAN DATA READ
//****************
float CANDataRead()
{
  unsigned char len = 0;
  unsigned char buf[8]; 

  if(CAN_MSGAVAIL == CAN.checkReceive())            // check if data coming
  {
    CAN.readMsgBuf(&len, buf);    // read data,  len: data length, buf: data buf
    unsigned int canId = CAN.getCanId();

    switch (canId) 
    {        
      case 0x181:
        current_data = buf[1];
        current_data <<= 8;
        current_data = current_data | buf[0];

        // Serial.print("Current Data: ");
        // Serial.println(current_data);
        break;
      case 0x381: // reads Position Actual Value

        // Serial.println("-----------------------------");
        // Serial.print("POSICAO: ");
        // Serial.println(canId, HEX);

        // for(int i = 0; i<len; i++)    // print the data
        // {
        //     Serial.print(buf[i], HEX);
        //     Serial.print("\t");
        // }
        // Serial.println();

        actualposition_data = buf[5];
        actualposition_data <<= 8;
        actualposition_data = actualposition_data | buf[4];
        actualposition_data <<= 8;
        actualposition_data = actualposition_data | buf[3];
        actualposition_data <<= 8;
        actualposition_data = actualposition_data | buf[2];

        Serial.print("Actual Position: ");
        Serial.println(actualposition_data);
        return(actualposition_data);

        break;

      // ID 321 message has information sent by the amplification board
      // Messages coming from the amp_board has most significative bits coming first
      case 0x321:
        // encoderDataRead();
        encoder_data = buf[4];
        encoder_data <<= 8; // bitshift equals times 2^8
        encoder_data = encoder_data | buf[5]; // sum operation

        // // load_cell information is read from buf[1], buf[2] and buf[3] and converted to decimal
        loadcell_data = buf[1];
        loadcell_data <<= 8;
        loadcell_data = loadcell_data | buf[2];
        loadcell_data <<= 8;
        loadcell_data = loadcell_data | buf[3];

        if(buf[1] >= 128)
          loadcell_data |= 0xFF000000;

        double loadcell_data_double = loadcell_data; 

        positionSetpoint(encoder_data);

        // Serial.print("Loadcell: ");
        // Serial.println(loadcell_data_double);

        // Serial.print("Encoder Position: ");
        // Serial.println(encoder_data);

        break;
      }
      return(encoder_data);
      return(loadcell_data_double);
  }
}

int readSerialInteger() // (not sure if this works, must test)
{
	while(1)
	{
		if(Serial.available())
		  { 
			  if(Serial.available()==1) return atoi(Serial.read());
			  return (Serial.parseInt());
		  }
	}
}

void serialController(char command)
{
	switch(command)
	{
		case 's': // Startup
			State = Startup;
			Serial.println("State: Startup");
		break;
		case 'p': // Pre_Operational
			State = Pre_Operational;
			Serial.println("State: Pre_Operational");
		break;
		case 'p': // Operational
			State = Operational;
			Serial.println("State: Operational");
		break;
		case 'c': // Constant CT_K (generic constant)
			CT_K = readSerialInteger();
			Serial.print("CT_K value: ");
			Serial.println(CT_K);
		break;
		case 'r': // Reset (not sure if this works, must test)
			while(1)
			{
			}
		break;
	}	
}

void loopModExo()
{
  switch (State) 
  {
    case Startup:
      doStartup();
      break;
    case Pre_Operational:
      PDOConfig();
      break;
    case Operational:
      CANDataRead();
	  if (sync_flag){
	    sync_flag=0;
	    sync();
	  }
      // positionSetpoint(encoder_data);
      break;
  }
  
  if(Serial.available())
  { 
      serialController(Serial.read());
  }
}

/*********************************************************************************************************
  END FILE
*********************************************************************************************************/