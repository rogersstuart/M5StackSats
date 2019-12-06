#include "ProgrammingMode.h"
#include "SerialConfig.h"
#include "ONSplash.c"
#include <M5Stack.h> 
#include <string.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <math.h>
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"
#include "AudioFileSourcePROGMEM.h"
#include "AudioClip.h"
#include "ASPLogo.h"
#include <esp_now.h>
#include "ProgrammingMode.h"
#include <EEPROM.h>
#include "Free_Fonts.h" // Include the header file attached to this sketch
#include "GlobalSettings.h"

#if CONFIG_FREERTOS_UNICORE
#define ARDUINO_RUNNING_CORE 0
#else
#define ARDUINO_RUNNING_CORE 1
#endif

#define KEYBOARD_I2C_ADDR     0X08
#define KEYBOARD_INT          5

#define PAYMENT_FAILED 0
#define PAYMENT_CANCELLED -1
#define PAYMENT_SUCCESS 1

#define INCLUDE_vTaskDelete 1

char wifiSSID[60] = "your_ssid_here";
char wifiPASS[60] = "your_password_here";

//BLITZ DETAILS
const char*  server = "api.opennode.co";
const int httpsPort = 443;
char api_key[60] = "your_api_key_here";
String description = "testing"; //invoice description
String hints = "false"; 

String choice = "";

String on_currency = "BTCUSD";  //currency can be changed here ie BTCEUR BTCGBP etc
String on_sub_currency = on_currency.substring(3);

String key_val;
String cntr = "0";
String inputs = "1";
int keysdec;
int keyssdec;
float temp;  
float satoshis;
String nosats = "";
float conversion;
String postid = "";
String data_id = "";
String data_lightning_invoice_payreq = "";
String data_status = "";
bool settle = false;
String payreq = "";
String hash = "";

float usd_to_bill = 100.00;
ulong ms_output_duration = 1000;

//audio

AudioFileSourcePROGMEM* in;
AudioGeneratorMP3* mp3;
AudioOutputI2S* out;

//wifi

WiFiUDP udp;
WiFiClient client;
IPAddress broadcast(255, 255, 255, 255);

//controls
bool remote_ip_is_valid = false;
IPAddress remote_ip;

bool output_state = true;

bool logo_drawn = false;
bool wifi_init_complete = false;

TaskHandle_t progress_bar_handle;
volatile float progress_bar_percentage = 0;

StaticJsonDocument<200> wifi_credentials;
char buffer[200];

//locks

_lock_t lcd_lock, wifi_lock, activ_lock;

char peer_address[6];

bool rates_init = false;

// The arduino task
void draw_logo_task(void* pvParameters)
{
	while (true)
	{
		_lock_acquire(&lcd_lock);
		
		M5.Lcd.drawJpg(logo, sizeof(logo));

		_lock_release(&lcd_lock);

		logo_drawn = true;

		vTaskDelete(NULL);
	}
	
}

void boot_audio_task(void* pvParameters)
{
	while (true)
	{
		in = new AudioFileSourcePROGMEM(audio_clip, sizeof(audio_clip));
		mp3 = new AudioGeneratorMP3();
		out = new AudioOutputI2S(0, 1);
		out->SetOutputModeMono(true);

		mp3->begin(in, out);

		while (mp3->isRunning())
		{
			if (!mp3->loop())
				mp3->stop();
		}

		vTaskDelete(NULL);

		delay(1000);
	}

}

void direct_wifi_programming_mode_task(void* pvParameters)
{
	while (true)
	{
		if (M5.BtnA.pressedFor(10000) && M5.BtnB.pressedFor(10000))
		{
			//connect to remote device

			EEPROM.write(0, 0xbf);
			EEPROM.commit();

			delay(100);

			ESP.restart();

		}
		else
			delay(10);
	}
	
}

void wifi_init()
{
	xTaskCreate(draw_boot_progressbar, "boot_progressbar", 1024, NULL, 1, &progress_bar_handle);
	
	while (true)
	{	
		progress_bar_percentage = 0;

		int num_ssid = 0;
		while ((num_ssid = WiFi.scanNetworks()) < 1)
			delay(1000);

		//at this point networks have been detected

		progress_bar_percentage = 0.25;

		int preferred_nets_rssi_0 = 0;
		bool preffered_net_is_present_0 = false;
		int preferred_nets_rssi_1 = 0;
		bool preffered_net_is_present_1 = false;

		WiFi.begin(wifiSSID, wifiPASS);

		WiFi.setAutoReconnect(true);

		//connect to local wifi            
		//
		float inc = 0.5 / 4000;
		for (int k = 0; k < 4000; k++)
		{
			if (WiFi.status() != WL_CONNECTED)
			{
				progress_bar_percentage += inc;
				delay(1);
			}	
			else
				if (WiFi.status() == WL_CONNECTED)
					break;
		}

		if (WiFi.status() == WL_CONNECTED)
			break;

		WiFi.disconnect();
	}

	progress_bar_percentage = 1;

	delay(500);

	vTaskDelete(progress_bar_handle);

	delay(100);

	wifi_init_complete = true;
}


void draw_boot_progressbar(void* pvParameters)
{
	float integ = 0;

	_lock_acquire(&lcd_lock);

	M5.Lcd.fillRect(10, 230, 300, 10, BLUE);

	_lock_release(&lcd_lock);
	
	while (true)
	{
		integ = (integ + progress_bar_percentage) * 0.5;
		
		int r = round(300 * integ);

		_lock_acquire(&lcd_lock);

		M5.Lcd.fillRect(10, 230, r, 10, progress_bar_percentage > integ ? GREEN : RED);
		M5.Lcd.fillRect(10+r, 230, 300-r, 10, BLUE);

		_lock_release(&lcd_lock);

		delay(41);
	}
}


////////////////////////////////////////  rate_task group /////////////////////////////////////////
bool rate_chk_status_complete = false;
void rate_task(void* pvParameters)
{
	while (true)
	{
		
		on_rates();
		rate_chk_status_complete = true;
		vTaskDelete(NULL);
		delay(10);
	}
}

TaskHandle_t rate_handle;
void rate_tracker_task(void* pvParameters)
{
	while (true)
	{
		//_lock_acquire(&wifi_lock);
		_lock_acquire(&activ_lock);

		xTaskCreate(rate_task, "rate_task", 8192, NULL, 1, &rate_handle);

		ulong rate_tmr = millis();
		while (((ulong)(millis() - rate_tmr) < 30000) && !rate_chk_status_complete)
			delay(100);
		
		if (!rate_chk_status_complete)
			ESP.restart();

		rate_chk_status_complete = false;

		//_lock_release(&wifi_lock);
		_lock_release(&activ_lock);

		delay(10000);
	}
	
}
////////////////////////////////////////////////////////////////////////////////////////////////////////

volatile bool client_write_complete = false;
volatile bool client_needs_writing = false;
void client_writing_task(void* pvParameters)
{
	//while (true)
	//{
		client.connect(remote_ip, 4001);

		if (client.connected())
		{
			client.write((uint8_t*)&ms_output_duration, 8);

			while (!client.available())
				delay(10);

			client_write_complete = true;

			client.stop();
		}
		

		vTaskDelete(NULL);

		//delay(100);
	//}
	
}


void udp_management_task(void* pvParameters)
{
	while (true)
	{
		update_remote_ip();

		delay(100);
	}
}

void serial_command_manager(void* pvParameters)
{
	while (true)
	{
		//_lock_acquire(&lcd_lock);
		parse_cmd();
		//_lock_release(&lcd_lock);
		
		delay(10);
	}
}

void m5_mgmt(void* pvParameters)
{
	while (true)
	{
		M5.update();

		delay(10);
	}
}

void setup() 
{
	Serial.begin(115200);
	M5.begin();
	
	EEPROM.begin(1024);

	delay(1000);

	M5.update();

	if (M5.BtnA.isPressed() && M5.BtnB.isPressed())
	{
		delay(6000);

		M5.update();
		if (M5.BtnA.pressedFor(4000) && !M5.BtnB.pressedFor(4000))
			ESP.restart();
		
		M5.Lcd.println("Continue to Hold to Clear Configuration Flags");
		
		int i = 0;

		while (true)
		{
			M5.update();
			if (M5.BtnA.pressedFor(12000) && M5.BtnB.pressedFor(12000))
			{
				EEPROM.write(0, 0);
				EEPROM.write(1, 0);
				EEPROM.commit();

				M5.Lcd.println("");
				M5.Lcd.println("Configuration Flags Cleared");
				M5.Lcd.println("Restarting in 4 seconds");

				delay(4000);

				ESP.restart();
			}
			else
				delay(200);

			if (!M5.BtnA.isPressed() || !M5.BtnB.isPressed())
				ESP.restart();
			else
				M5.Lcd.print('.');
		}
	}

	EEPROM.get(EEPROM_PEER_ADDRESS, peer_address);

	
	
	//check to see if wifi credentials have been saved to eeprom
	if (EEPROM.read(1) == 0xaf)
	{
		StaticJsonDocument<200> wifi_credentials;

		char buffer[200];
		EEPROM.get(2, buffer);

		deserializeJson(wifi_credentials, buffer);

		const char* p1 = wifi_credentials["ssid"];
		const char* p2 = wifi_credentials["password"];

		memcpy(wifiSSID, p1, strlen(p1));
		memcpy(wifiPASS, p2, strlen(p2));


		StaticJsonDocument<200> other_settings;
		char buffer2[200];
		memset(buffer2, 0, 200);

		EEPROM.get(EEPROM_ADDITIONAL_PARAMS, buffer2);

		deserializeJson(other_settings, buffer2);

		const char* p3 = other_settings["api_key"];

		memcpy(api_key, p1, strlen(p3));

		usd_to_bill = other_settings["to_bill"];
		ms_output_duration = other_settings["active_duration"];

		Serial.println(ms_output_duration);
	}
	

	//esp now entry key is stored at address 0
	if (EEPROM.read(0) == 0xbf)
	{
		EEPROM.write(0, 0x00);
		EEPROM.commit();

		//credential programming mode

		//make sure that the user intended to do this
		M5.update();
		if (M5.BtnA.isPressed())
		{
			delay(2000);

			M5.update();
			if (!M5.BtnA.isPressed())
				ESP.restart();
		}
		//////////////////////////////////////////////

		wifi_credentials["ssid"] = wifiSSID;
		wifi_credentials["password"] = wifiPASS;

		serializeJson(wifi_credentials, buffer, 200);

		prog_setup();

		ulong tmr = millis();
		while (true)
		{
			M5.update();

			prog_loop((uint8_t*)buffer, sizeof(buffer));

			//if this has been going on for more than 30 seconds then reboot
			if ((ulong)((long)millis() - tmr) >= 30000)
				ESP.restart();
			else
				delay(1);
		}
	}
	//
		
	
	xTaskCreate(serial_command_manager, "serial_commands", 8192, NULL, 10, NULL);

	//Wire.begin();
	pinMode(KEYBOARD_INT, INPUT_PULLUP);

	xTaskCreate(draw_logo_task, "draw_logo", 8192, NULL, 1, NULL);
	//xTaskCreate(boot_audio_task, "boot_audio", 8192, NULL, 1, NULL);

	while (!logo_drawn)
		delay(1);

	wifi_init();

	//wifi has connected

	if (!udp.begin(4000))
		ESP.restart();

	delay(100);
  
	xTaskCreate(m5_mgmt, "m5_mgmt", 1024, NULL, 2, NULL);
	delay(1);
	xTaskCreate(rate_tracker_task, "rate_tracker", 4096, NULL, 2, NULL);
	delay(1);
	xTaskCreate(udp_management_task, "udp_manager", 4096, NULL, 1, NULL);
	delay(1);
	xTaskCreate(direct_wifi_programming_mode_task, "direct_wifi", 1024, NULL, 2, NULL);
	delay(1);
}

void page_input(bool en)
{
	_lock_acquire(&lcd_lock);

	if(en)
	M5.Lcd.fillScreen(BLACK);
	
	M5.Lcd.setFreeFont(FSB18);
	M5.Lcd.setTextColor(TFT_WHITE);
	M5.Lcd.setTextSize(1);
	M5.Lcd.setCursor(11, 24);

	if (en)
	{

		M5.Lcd.println("American Standard");
		M5.Lcd.println("             Power");
	}

	if (!en)
	{
		M5.Lcd.fillRect(0, 80, 320, 240, BLACK);
	}

	M5.Lcd.setTextSize(1);
	M5.Lcd.setFreeFont(FS9);
	M5.Lcd.println("");
	M5.Lcd.setFreeFont(FSB12);
	M5.Lcd.setCursor(12, 125);
	M5.Lcd.println("USD: ");
	M5.Lcd.setCursor(2, 160);
	M5.Lcd.println("SATS: ");
	M5.Lcd.setCursor(170, 125);
	M5.Lcd.println("TIME: ");

	//M5.Lcd.setCursor(30, 230);
	//M5.Lcd.println("RESET");

	M5.Lcd.setCursor(225, 230);
	M5.Lcd.println("BILL");

	_lock_release(&lcd_lock);
}

void page_processing()
{
	_lock_acquire(&lcd_lock);
	
	M5.Lcd.fillScreen(BLACK);
	M5.Lcd.setCursor(45, 80);
	M5.Lcd.setFreeFont(FSB18);
	M5.Lcd.setTextSize(1);
	M5.Lcd.setTextColor(TFT_WHITE);
	M5.Lcd.println("PROCESSING");

	_lock_release(&lcd_lock);
}

void page_loading(bool en)
{
	_lock_acquire(&lcd_lock);

	M5.Lcd.fillScreen(BLACK);

	M5.Lcd.setFreeFont(FSB18);
	M5.Lcd.setTextColor(TFT_WHITE);
	M5.Lcd.setTextSize(1);
	M5.Lcd.setCursor(11, 24);
	M5.Lcd.println("American Standard");
	M5.Lcd.println("             Power");

	M5.Lcd.setCursor(80, 160);
	M5.Lcd.setFreeFont(FSB18);
	M5.Lcd.setTextSize(1);
	M5.Lcd.setTextColor(TFT_WHITE);

	if(en)
	M5.Lcd.print("Loading");
	else
		M5.Lcd.print("Loading...");


	if(en)
	for (int i = 0; i < 6; i++)
	{
		delay(1000);
		M5.Lcd.print(".");
	}

	_lock_release(&lcd_lock);
}

/*
void get_keypad()
{

	if (digitalRead(KEYBOARD_INT) == LOW)
	{
		Wire.requestFrom(KEYBOARD_I2C_ADDR, 1);  // request 1 byte from keyboard
		while (Wire.available())
		{
			uint8_t key = Wire.read();                  // receive a byte as character
			key_val = key;

			if (key != 0)
			{
				if (key >= 0x20 && key < 0x7F)
				{

					// ASCII String
					if (isdigit((char)key))
					{
						key_val = ((char)key);
					}
					else
					{
						key_val = "";
					}

				}
			}
		}
	}
}
*/

void page_qrdisplay(String xxx)
{
	_lock_acquire(&lcd_lock);

	M5.Lcd.fillScreen(BLACK);
	M5.Lcd.qrcode(payreq, 45, 0, 240, 10);

	_lock_release(&lcd_lock);
}

char buffer1234[128];
StaticJsonDocument<128> doc1234;
void update_remote_ip()
{
	int pkt_size = udp.parsePacket();
	
	if (pkt_size == 128)
	{
		while (true)
		{
			

			//remote_ip_is_valid = false;

			udp.readBytes(buffer1234, 128);

			//Serial.println(buffer1234);

			deserializeJson(doc1234, buffer1234);

			if (doc1234["mac"].isNull())
				break;
			else
			{
				char rem_mac[6];
				const char* mac = doc1234["mac"];

				memcpy(rem_mac, mac, 6);

				bool flag = false;
				for (int i = 0; i < 6; i++)
					if (peer_address[i] == rem_mac[i])
						continue;
					else
					{
						flag = true;
						break;
					}

				if (flag)
				{
					/*
					Serial.println("mac didn't match");

					for (int i = 0; i < 6; i++)
						Serial.print(rem_mac[i], HEX);
					Serial.println("");

					for (int i = 0; i < 6; i++)
						Serial.print(peer_address[i],HEX);
					Serial.println("");
					*/
					
					
					break;
				}
					
			}

			if (doc1234["ip"].isNull())
				break;

			int to[4];

			JsonArray arr = doc1234.getMember("ip");
			for(int i = 0; i < 4; i++)
				to[i] = arr.getElement(i);


			IPAddress remote_device(to[0], to[1], to[2], to[3]);

			//Serial.println(remote_device.toString());

			remote_ip = remote_device;

			remote_ip_is_valid = true;

			

			break;
		}

		udp.flush();
	}
	
}

bool invoice_req_complete = false;
void req_invoice(void* pvParameters)
{
	while (true)
	{
		reqinvoice(nosats);
		invoice_req_complete = true;
		vTaskDelete(NULL);
		delay(10);
	}
	
}

bool chk_status_complete = false;
void chk_status(void* pvParameters)
{
	while (true)
	{
		checkpayment(data_id);
		chk_status_complete = true;
		vTaskDelete(NULL);
		delay(10);
	}

}

String last_nosats = "";

void loop()
{
	page_loading(true);

	while (!rates_init)
		delay(100);
	
	/*
	_lock_acquire(&lcd_lock);
	
	M5.Lcd.fillScreen(BLACK);
	M5.Lcd.setCursor(0, 0);

	_lock_release(&lcd_lock);
	*/

	//draw main text
	page_input(false);

	while (true)
	{
		//if (WiFi.status() != WL_CONNECTED)
		//	ESP.restart();

		//M5.update();
		//get_keypad(); 

		if (M5.BtnC.isPressed())
		{
			//acquire activ_lock to prevent the rate from being updated unnecessarily

			page_loading(false);
			
			_lock_acquire(&activ_lock);
			handle_invoice_request();
			_lock_release(&activ_lock);

			//draw main text
			page_input(true);
		}
		
		temp = usd_to_bill;

		satoshis = temp/conversion;

		int intsats = (int) round(satoshis*100000000.0);

		nosats = String(intsats);

		_lock_acquire(&lcd_lock);
		M5.Lcd.setTextSize(1);
		M5.Lcd.setFreeFont(FS12);
		M5.Lcd.setTextColor(TFT_RED, TFT_BLACK);
		M5.Lcd.setCursor(74, 125);
		M5.Lcd.print("$");
		M5.Lcd.println(usd_to_bill);

		M5.Lcd.setCursor(246, 125);
		M5.Lcd.print((int)(ms_output_duration/1000.0));
		M5.Lcd.println("s");


		//print nosats
		M5.Lcd.setTextColor(TFT_GREEN, TFT_BLACK);
		if (nosats != last_nosats)
		{
			//M5.Lcd.setCursor(160, 120);
			M5.Lcd.fillRect(69,139,170, 22, BLACK);

			last_nosats = nosats;
		}

		M5.Lcd.setCursor(74, 160);
		M5.Lcd.println(nosats);
		//

		_lock_release(&lcd_lock);

		delay(100);
	}
}

void handle_invoice_request()
{
	//display processing text
	page_processing();

	//create a task for utilizing the api to request an invoice
	TaskHandle_t handle;
	//_lock_acquire(&wifi_lock);
	xTaskCreate(req_invoice, "req_invoice", 8192, NULL, 1, &handle);
	//

	//if the api hasn't responded in a certain amount of time then cancel the task and return
	ulong inv_tmr = millis();
	while (!invoice_req_complete)
	{
		if ((ulong)(millis() - inv_tmr) >= 20000)
		{
			vTaskDelete(handle);
		//	_lock_release(&wifi_lock);
			return;
		}
		else
			delay(10);
	}
	//

	//_lock_release(&wifi_lock);

	invoice_req_complete = false;
	///////////////////////////////////////////////////////////

	//display the invoice qr code
	page_qrdisplay(payreq);

	
	settle = false;

	//wait for the user to release button c before continuing
	while (M5.BtnC.isPressed())
		delay(10);

	ulong overall_tmr = millis();
	while (true)
	{
		//check the payment status

		//create a task for utilizing the api to check payment status
		TaskHandle_t handle;
		//

		//if the api hasn't responded in a certain amount of time then cancel the task and try again
		ulong stat_tmr = millis();
		ulong tot_tmr = millis();
		while (!chk_status_complete)
		{
		//	_lock_acquire(&wifi_lock);
			xTaskCreate(&chk_status, "chk_status", 8192, NULL, 1, &handle);
			
			//delay for two seconds
			while ((ulong)(millis() - stat_tmr) < 8000)
			{
				if (chk_status_complete)
					break;
				else
					if (M5.BtnA.pressedFor(1000))
					{
						vTaskDelete(handle);
			//			_lock_release(&wifi_lock);
						post_invoice(PAYMENT_CANCELLED);
						return;
					}
					else
						delay(10);
			}

			if (!chk_status_complete)
			{
				vTaskDelete(handle);
				//_lock_release(&wifi_lock);
			}

			delay(100);

			if (((ulong)(millis() - tot_tmr) >= 18000) && !chk_status_complete)
			{
				post_invoice(PAYMENT_FAILED);
				return;
			}
			else
				if (M5.BtnA.pressedFor(1000))
				{
					post_invoice(PAYMENT_CANCELLED);
					return;
				}
		}
		//

		chk_status_complete = false;
		///////////////////////////////////////////////////////////

		//
		if (data_status != "unpaid")
		{
			post_invoice(PAYMENT_SUCCESS);
			break;
		}
		else
		{
			if (M5.BtnA.pressedFor(1000))
			{
				post_invoice(PAYMENT_CANCELLED);
				break;
			}
			else
				if (((ulong)(millis() - overall_tmr) >= 120000))
				{
					post_invoice(PAYMENT_FAILED);
					break;
				}
				else
					delay(10);
		}	
	}

	
}

//-1 cancelled, 0 failed, 1 success
void post_invoice(int code)
{
	data_lightning_invoice_payreq = "";
	payreq = "";
	
	if (code == PAYMENT_CANCELLED)
	{
		
		_lock_acquire(&lcd_lock);
		M5.Lcd.fillScreen(BLACK);
		M5.Lcd.setCursor(50, 80);
		M5.Lcd.setFreeFont(FSB18);
		M5.Lcd.setTextSize(1);
		M5.Lcd.setTextColor(TFT_RED);
		M5.Lcd.println("CANCELLED");
		_lock_release(&lcd_lock);

		

		//testing, turn output on
		//digitalWrite(3, LOW);

		delay(2000);

		page_input(true);
	}
	else
		if (code == PAYMENT_FAILED)
		{
			_lock_acquire(&lcd_lock);
			M5.Lcd.fillScreen(BLACK);
			M5.Lcd.setCursor(88, 80);
			M5.Lcd.setFreeFont(FSB18);
			M5.Lcd.setTextSize(1);
			M5.Lcd.setTextColor(TFT_RED);
			M5.Lcd.println("FAILED");
			_lock_release(&lcd_lock);

			//testing, turn output on
			//digitalWrite(3, LOW);

			delay(2000);

			page_input(true);
		}
		else
			if (code == PAYMENT_SUCCESS)
			{
				
				_lock_acquire(&lcd_lock);

				M5.Lcd.fillScreen(BLACK);
				M5.Lcd.setCursor(60, 80);
				M5.Lcd.setFreeFont(FSB18);
				M5.Lcd.setTextSize(1);
				M5.Lcd.setTextColor(TFT_GREEN);
				M5.Lcd.println("COMPLETE");

				_lock_release(&lcd_lock);

				//paid, turn output on
				//digitalWrite(3, LOW);
				//output_state = false;

				delay(2000);

				
				
				_lock_acquire(&lcd_lock);
				M5.Lcd.fillScreen(BLACK);
				M5.Lcd.setCursor(122, 80);
				M5.Lcd.setFreeFont(FSB18);
				M5.Lcd.setTextSize(1);
				M5.Lcd.setTextColor(TFT_WHITE);
				M5.Lcd.println("SYNC");
				_lock_release(&lcd_lock);
				
				while (true)
				{
					TaskHandle_t client_handle;
					xTaskCreate(client_writing_task, "client_task", 4096, NULL, 1, &client_handle);

					ulong client_tmr = millis();
					while (((ulong)((long)millis() - client_tmr) < 2000) && !client_write_complete)
						delay(100);

					if (client_write_complete)
						break;
					else
					{
						vTaskDelete(client_handle);
						delay(100);
					}
				}

				client_write_complete = false;

				_lock_acquire(&lcd_lock);
				M5.Lcd.fillScreen(BLACK);
				_lock_release(&lcd_lock);


				ulong timer = millis();

				int old_sec = -1;
				int old_percent = -1;
				while ((ulong)((long)millis() - timer) < ms_output_duration)
				{
					ulong value = (millis() - timer);
					int seconds = round(value * 0.001);
					float percent = value * (1.0/ms_output_duration);

					_lock_acquire(&lcd_lock);

					
					//Serial.println(millis());


					//M5.Lcd.fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
					int width = 220 - (int)round(((float)value * (1.0 / (float)ms_output_duration)) * 220);

					M5.Lcd.fillRect(50+width, 120, 220-width, 20, RED);
					M5.Lcd.fillRect(50, 120, width, 20, GREEN);

					int new_sec = seconds;
					int new_percent = (int)round(percent * 100);

					if ((new_sec != old_sec) || (new_percent != old_percent))
					{
						old_sec = new_sec;
						old_percent = new_percent;

						M5.Lcd.fillRect(50, 77, 180, 26, BLACK);
						M5.Lcd.setTextColor(TFT_WHITE);
						M5.Lcd.setFreeFont(FSB18);
						M5.Lcd.setCursor(50, 100);
						M5.Lcd.setTextSize(1);
						M5.Lcd.print(seconds);
						M5.Lcd.print("s  ");
						M5.Lcd.print(new_percent);
						M5.Lcd.print("%");
					}

					_lock_release(&lcd_lock);

					delay(20);
				}

				//digitalWrite(3, HIGH);
				//output_state = true;

				_lock_acquire(&lcd_lock);

				M5.Lcd.fillScreen(BLACK);
				M5.Lcd.setTextColor(TFT_WHITE);

				_lock_release(&lcd_lock);
				page_input(true);
			}
}

///////////////////////////////////////////////////////////////////////////////////////////////

//OPENNODE REQUESTS

void on_rates()
{
	WiFiClientSecure client;
  
	if (!client.connect(server, httpsPort))
		return;
  
	String url = "/v1/rates";
  
	client.print(String("GET ") + url + " HTTP/1.1\r\n" +
				"Host: " + server + "\r\n" +
				"User-Agent: ESP32\r\n" +
				"Connection: close\r\n\r\n");
               
	while (client.connected())
	{
		String line = client.readStringUntil('\n');
    
		if (line == "\r")
			break;
	}
  
	String line = client.readStringUntil('\n');
  
	const size_t capacity = 169*JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(168) + 3800;
	DynamicJsonDocument doc(capacity);

	deserializeJson(doc, line);

	conversion = doc["data"][on_currency][on_currency.substring(3)]; 

	rates_init = true;
}

void reqinvoice(String value)
{
	WiFiClientSecure client;

	if (!client.connect(server, httpsPort)) 
		ESP.restart();

	String topost = "{  \"amount\": \""+ value +"\", \"description\": \""+ description  +"\", \"route_hints\": \""+ hints  +"\"}";
	String url = "/v1/charges";

	client.print(String("POST ") + url + " HTTP/1.1\r\n" +
					"Host: " + server + "\r\n" +
					"User-Agent: ESP32\r\n" +
					"Authorization: " + api_key + "\r\n" +
					"Content-Type: application/json\r\n" +
					"Connection: close\r\n" +
					"Content-Length: " + topost.length() + "\r\n" +
					"\r\n" + 
					topost + "\n");

	while (client.connected())
	{
		String line = client.readStringUntil('\n');
		if (line == "\r")
			break;
	}

	String line = client.readStringUntil('\n');

	const size_t capacity = 169*JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(168) + 3800;
	DynamicJsonDocument doc(capacity);

	deserializeJson(doc, line);

	String data_idd = doc["data"]["id"]; 
	data_id = data_idd;
	String data_lightning_invoice_payreqq = doc["data"]["lightning_invoice"]["payreq"];
	payreq = data_lightning_invoice_payreqq;
}


void checkpayment(String PAYID)
{
	WiFiClientSecure client;

	if (!client.connect(server, httpsPort))
		ESP.restart();

	String url = "/v1/charge/" + PAYID;

	client.print(String("GET ") + url + " HTTP/1.1\r\n" +
				"Host: " + server + "\r\n" +
				"Authorization: " + api_key + "\r\n" +
				"User-Agent: ESP32\r\n" +
				"Connection: close\r\n\r\n");

	while (client.connected())
	{
		String line = client.readStringUntil('\n');
		if (line == "\r")
			break;
	}

	String line = client.readStringUntil('\n');

	const size_t capacity = JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(4) + JSON_OBJECT_SIZE(14) + 650;
	DynamicJsonDocument doc(capacity);

	deserializeJson(doc, line);

	String data_statuss = doc["data"]["status"]; 
	data_status = data_statuss;
}
