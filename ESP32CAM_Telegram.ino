/*
 Name:        ESP32CAM_Telegram.ino
 Created:     15/08/2021
 Author:      Serhii
 Description: Is possible send an image captured from a ESP32-CAM board
 fnd save file to SD card
*/

//                                             WARNING!!!
// Make sure that you have selected ESP32 Wrover Module, or another board which has PSRAM enabled
// and Partition Schema: "Default 4MB with ffat..."
#define ESP32 true

#include "esp_camera.h"
#include <WiFi.h>

#include <AsyncTelegram.h>
#include "soc/soc.h"          // Brownout error fix
#include "soc/rtc_cntl_reg.h" // Brownout error fix

#pragma region Defines

#define USE_MMC true // Define where store images (on board SD card reader or internal flash memory)
#define FILENAME_SIZE 20
#define KEEP_IMAGE true

#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27

//CAMERA_MODEL_AI_THINKER
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

#define FLASH_LED 4
#define LED_BUILTIN 2

#define LIGHT_ON_CALLBACK "lightON"   // callback data sent when "LIGHT ON" button is pressed
#define LIGHT_OFF_CALLBACK "lightOFF" // callback data sent when "LIGHT OFF" button is pressed
#define DEBUG_ENABLE false

#define CAMERS_SAVE_TIMEOUT 600000 // 10 minut

#pragma endregion

#ifdef USE_MMC
#include <SD_MMC.h> // Use onboard SD Card reader
fs::FS &filesystem = SD_MMC;
#else
#include <FFat.h> // Use internal flash memory
fs::FS &filesystem = FFat; // Is necessary select the proper partition scheme (ex. "Default 4MB with ffta..")
                           // You only need to format FFat when error on mount (don't work with MMC SD card)
#define FORMAT_FS_IF_FAILED true
#endif

AsyncTelegram myBot;
ReplyKeyboard myReplyKbd;   // reply keyboard object helper
InlineKeyboard myInlineKbd; // inline keyboard object helper

#pragma region Variable
bool isKeyboardActive; // store if the reply keyboard is shown

// REPLACE myPassword YOUR WIFI PASSWORD, IF ANY
const char *ssid_0 = "********";
const char *pass_0 = "********";
const char *ssid_1 = "********";
const char *pass_1 = "********";

const char *token = "********:********************************"; // REPLACE myToken WITH YOUR TELEGRAM BOT TOKEN
//chat_id: 387342374
#pragma endregion

// Struct for saving time datas (needed for time-naming the image files)
struct tm timeinfo;

#pragma region Task
TaskHandle_t Task_Message;
TaskHandle_t task_Message_Handler = NULL;
TaskHandle_t Task_Picture;
TaskHandle_t task_Picture_Handler = NULL;
TaskHandle_t task_WiFi;
TaskHandle_t task_WiFi_Handler = NULL;
#pragma region

#pragma region Semaphores
SemaphoreHandle_t serial_Mutex = NULL;
SemaphoreHandle_t camera_Semaphore;
#pragma endregion

void cameraSetup(framesize_t frameSize)
{
    /* pinMode(FLASH_LED, OUTPUT);
    // configure LED PWM functionalitites (ledChannel, freq, resolution);
    ledcSetup(15, 5000, 8);
    ledcAttachPin(FLASH_LED, 15);
    ledcWrite(15, 0); // Flash led OFF */

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.fb_count = 1;
    //init with high specs to pre-allocate larger buffers
    if (psramFound() && frameSize == FRAMESIZE_UXGA)
    {
        config.frame_size = FRAMESIZE_UXGA;
        config.jpeg_quality = 10;
    }
    else
    {
        config.frame_size = frameSize;
        config.jpeg_quality = 15;
    }

#if defined(CAMERA_MODEL_ESP_EYE)
    pinMode(13, INPUT_PULLUP);
    pinMode(14, INPUT_PULLUP);
#endif

    // camera init
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        Serial.printf("Camera init failed with error 0x%x", err);
        ESP.restart();
        return;
    }
    else
    {
        Serial.println("OK!");
    }

    sensor_t *s = esp_camera_sensor_get();
    //initial sensors are flipped vertically and colors are a bit saturated
    /* if (s->id.PID == OV3660_PID)
	{
		s->set_vflip(s, 1);		  //flip it back
		s->set_brightness(s, 1);  //up the blightness just a bit
		s->set_saturation(s, -2); //lower the saturation
	} */

#if defined(CAMERA_MODEL_M5STACK_WIDE)
    s->set_vflip(s, 1);
    s->set_hmirror(s, 1);
#endif
}

bool SD_MMC_Setup()
{
    // Init filesystem
    Serial.print("Setup SD MMC ... ");
#ifdef USE_MMC
    if (!SD_MMC.begin())
    {
        Serial.println("SD Card Mount Failed");
        return false;
    }
    else
    {
        if (SD_MMC.cardType() == CARD_NONE)
        {
            Serial.println("No SD Card attached");
            return false;
        }
        return true;
    }

#else
    // Init filesystem (format if necessary)
    if (!FFat.begin(FORMAT_FS_IF_FAILED))
        Serial.println("\nFS Mount Failed.\nFilesystem will be formatted, please wait.");
    Serial.printf("\nTotal space: %10lu\n", FFat.totalBytes());
    Serial.printf("Free space: %10lu\n", FFat.freeBytes());
    return true;
#endif

    //listDir(filesystem, "/", 0);
}

// List all files saved in the selected filesystem
String listDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
    //char _buffer[128] = {};
    String msg_txt = "List Dir:\n";
    xSemaphoreTake(serial_Mutex, portMAX_DELAY);
    Serial.printf("Listing directory: %s\r\n", dirname);

    File root = fs.open(dirname);
    if (!root)
    {
        Serial.println("- failed to open directory");
        return "";
    }
    if (!root.isDirectory())
    {
        Serial.println(" - not a directory");
        return "";
    }

    File file = root.openNextFile();
    while (file)
    {
        if (file.isDirectory())
        {
            Serial.printf("  DIR : %s \n", file.name());
            msg_txt += file.name();
            msg_txt += "\n";
            if (levels)
                listDir(fs, file.name(), levels - 1);
        }
        else
        {
            Serial.printf(" FILE: %s\tSIZE: %d\n", file.name(), file.size());
            msg_txt += file.name();
            msg_txt += ". Size = ";
            msg_txt += file.size();
            msg_txt += "\n";
            //sprintf(_buffer, " %s\n", file.name());
            //strcat(msg_txt, _buffer);
        }
        file = root.openNextFile();
    }
    xSemaphoreGive(serial_Mutex);
    return msg_txt;
}

String Open_File(fs::FS &fs, String Name_File)
{
    // Path where new picture will be saved
    String path = "/";
    path += String(Name_File);
    Serial.printf("Open file ... %s ", Name_File);

    File file = fs.open(path.c_str(), FILE_READ);
    byte tyres = 5;
    while (--tyres && !file)
    {
        Serial.println("Failed to open file in reading mode");
        delay(500);
        if (!SD_MMC_Setup())
        {
            return "";
        }
        else
        {
            Serial.println(" OK!");
            file = fs.open(path.c_str(), FILE_WRITE);
        }
        delay(100);
    }

    if (!file)
    {
        Serial.println("Sory for 5 setup SD csrd no mode");
        return "";
    }

    return path;
}

String takePicture(fs::FS &fs)
{

    // Set filename with current timestamp "YYYYMMDD_HHMMSS.jpg"
    char pictureName[FILENAME_SIZE];
    getLocalTime(&timeinfo);
    snprintf(pictureName, FILENAME_SIZE, "%02d%02d%02d_%02d%02d%02d.jpg", timeinfo.tm_year + 1900,
             timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    // Path where new picture will be saved
    String path = "/";
    path += String(pictureName);
    Serial.printf("Save file ... %s ", pictureName);

    File file = fs.open(path.c_str(), FILE_WRITE);
    byte tyres = 5;
    while (--tyres && !file)
    {
        Serial.println("Failed to open file in writing mode");
        delay(500);
        if (!SD_MMC_Setup())
        {
            return "";
        }
        else
        {
            Serial.println(" OK!");
            file = fs.open(path.c_str(), FILE_WRITE);
        }
        delay(100);
    }

    if (!file)
    {
        Serial.println("Sory for 5 setup SD csrd no mode");
        return "";
    }

    // Take Picture with Camera
    //ledcWrite(15, 50); // Flash led ON
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb)
    {
        Serial.println("Camera capture failed");
        return "";
    }
    //ledcWrite(15, 0); // Flash led OFF

    // Save picture to memory
#ifdef USE_MMC
    uint64_t freeBytes = SD_MMC.totalBytes() - SD_MMC.usedBytes();
#else
    uint64_t freeBytes = FFat.freeBytes();
#endif

    if (freeBytes > fb->len)
    {
        file.write(fb->buf, fb->len); // payload (image), payload length
                                      //Serial.printf("Saved file to path: %s\n", path.c_str());
        file.close();
        // Take picture and save to file
        Serial.println("Photo save is ok!");
    }
    else
        Serial.println("Not enough space avalaible");
    esp_camera_fb_return(fb);
    return path;
}

void Task_Message_Code(void *args)
{
    bool file_From_SD = false;
    while (true)
    {
        // a variable to store telegram message data
        TBMessage msg;
        // if there is an incoming message...
        if (myBot.getNewMessage(msg))
        {
            vTaskSuspend(task_WiFi_Handler);
            // check what kind of message I received
            String tgReply;
            MessageType msgType = msg.messageType;

            switch (msgType)
            {
            case MessageText:

                // received a text message
                tgReply = msg.text;
                Serial.print("\nText message received: ");
                Serial.println(tgReply);

                // check if is show keyboard command
                if (tgReply.equalsIgnoreCase("/reply_keyboard"))
                {
                    // the user is asking to show the reply keyboard --> show it
                    myBot.sendMessage(msg, "This is reply keyboard:", myReplyKbd);
                    isKeyboardActive = true;
                }
                else if (tgReply.equalsIgnoreCase("/inline_keyboard"))
                {
                    myBot.sendMessage(msg, "This is inline keyboard:", myInlineKbd);
                }

                // check if the reply keyboard is active
                else if (isKeyboardActive)
                {
                    // is active -> manage the text messages sent by pressing the reply keyboard buttons
                    if (tgReply.equalsIgnoreCase("/hide_keyboard"))
                    {
                        // sent the "hide keyboard" message --> hide the reply keyboard
                        myBot.removeReplyKeyboard(msg, "Reply keyboard removed");
                        isKeyboardActive = false;
                    }
                    else
                    {
                        if (msg.text == "Take photo")
                        {
                            Serial.println("\nSending Photo from CAM");
                            vTaskSuspend(task_Picture_Handler);
                            // Take picture and save to file
                            String myFile = takePicture(filesystem);
                            if (myFile != "")
                            {
                                if (!myBot.sendPhotoByFile(msg.sender.id, myFile, filesystem))
                                {
                                    Serial.println("Photo send failed");
                                }
                                Serial.println("Photo send is ok!");
                                //If you don't need to keep image in memory, delete it
                                if (KEEP_IMAGE == false)
                                {
                                    filesystem.remove("/" + myFile);
                                }
                            }
                            vTaskResume(task_Picture_Handler);
                        }
                        else if (msg.text == "Save photo")
                        {
                            vTaskSuspend(task_Picture_Handler);
                            Serial.print("Sending Photo from CAM. ");
                            // Take picture and save to file
                            String myFile = takePicture(filesystem);
                            if (myFile != "")
                            {
                                Serial.println("Photo save is ok!");
                                char file_name[128];
                                sprintf(file_name, "Save file name - %s", myFile.c_str());
                                myBot.sendMessage(msg, file_name);
                            }
                            vTaskResume(task_Picture_Handler);
                        }
                        else if (msg.text == "List dir SD")
                        {
                            String listing = listDir(filesystem, "/", 0);
                            // print every others messages received
                            myBot.sendMessage(msg, listing);
                        }
                        else if (msg.text == "File from SD")
                        {
                            if (!file_From_SD)
                            {
                                file_From_SD = true;
                                myBot.sendMessage(msg, "Input file name in format /name_File");
                            }
                        }
                        else
                        {
                            if (file_From_SD)
                            {
                                file_From_SD = false;
                                String myFile = Open_File(filesystem, String(msg.text));
                                if (myFile != "")
                                {
                                    if (!myBot.sendPhotoByFile(msg.sender.id, myFile, filesystem))
                                    {
                                        Serial.println("Photo send from SD is failed");
                                    }
                                    Serial.println("Photo send from SD is ok!");
                                }
                                myBot.sendMessage(msg, "I`m sorry this file not in SD card");
                            }
                            else
                            { // print every others messages received
                                myBot.sendMessage(msg, msg.text);
                            }
                        }
                    }
                }

                // the user write anything else and the reply keyboard is not active --> show a hint message
                else
                {
                    myBot.sendMessage(msg, "Try /reply_keyboard or /inline_keyboard");
                }
                break;

            case MessageQuery:
                // received a callback query message
                tgReply = msg.callbackQueryData;
                xSemaphoreTake(serial_Mutex, portMAX_DELAY);
                Serial.print("\nCallback query message received: ");
                Serial.println(tgReply);
                xSemaphoreGive(serial_Mutex);

                if (tgReply.equalsIgnoreCase(LIGHT_ON_CALLBACK))
                {
                    // pushed "LIGHT ON" button...
                    xSemaphoreTake(serial_Mutex, portMAX_DELAY);
                    Serial.println("\nSet light ON");
                    xSemaphoreGive(serial_Mutex);
                    //digitalWrite(LED, HIGH);
                    // terminate the callback with an alert message
                    myBot.endQuery(msg, "Light on", true);
                }
                else if (tgReply.equalsIgnoreCase(LIGHT_OFF_CALLBACK))
                {
                    // pushed "LIGHT OFF" button...
                    xSemaphoreTake(serial_Mutex, portMAX_DELAY);
                    Serial.println("\nSet light OFF");
                    xSemaphoreGive(serial_Mutex);
                    //digitalWrite(LED, LOW);
                    // terminate the callback with a popup message
                    myBot.endQuery(msg, "Light off");
                }

                break;

            case MessageLocation:
                // received a location message --> send a message with the location coordinates
                char bufL[50];
                snprintf(bufL, sizeof(bufL), "Longitude: %f\nLatitude: %f\n", msg.location.longitude, msg.location.latitude);
                myBot.sendMessage(msg, bufL);
                xSemaphoreTake(serial_Mutex, portMAX_DELAY);
                Serial.println(bufL);
                xSemaphoreGive(serial_Mutex);
                break;

            case MessageContact:
                char bufC[50];
                snprintf(bufC, sizeof(bufC), "Contact information received: %s - %s\n", msg.contact.firstName, msg.contact.phoneNumber);
                // received a contact message --> send a message with the contact information
                myBot.sendMessage(msg, bufC);
                xSemaphoreTake(serial_Mutex, portMAX_DELAY);
                Serial.println(bufC);
                xSemaphoreGive(serial_Mutex);
                break;

            default:
                break;
            }
            vTaskResume(task_WiFi_Handler);
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

void Task_Picture_Code(void *args)
{
    while (true)
    {
        //First picter wait  CAMERS_SAVE_TIMEOUT
        vTaskDelay(CAMERS_SAVE_TIMEOUT / portTICK_PERIOD_MS);
        vTaskSuspend(task_WiFi_Handler);
        vTaskSuspend(task_Message_Handler);
        xSemaphoreTake(serial_Mutex, portMAX_DELAY);
        Serial.print("Sending Photo from CAM. ");
        // Take picture and save to file
        String myFile = takePicture(filesystem);
        /* if (myFile != "")
        {
            //Serial.printf("Save file name - %s is OK!", myFile.c_str());
            String _buffer_message;
            _buffer_message = "Save file name -";
            _buffer_message += myFile.c_str();
            myBot.sendTo( 387342374,_buffer_message,"");
        } */
        xSemaphoreGive(serial_Mutex);
        vTaskResume(task_WiFi_Handler);
        vTaskResume(task_Message_Handler);
    }
}

void Task_WiFi_Code(void *args)
{
    // Init WiFi connections
    Serial.printf("WiFi task running on core  %d\n", xPortGetCoreID());

    while (true)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            vTaskDelay(60000 / portTICK_PERIOD_MS);
        }
        else
        {
            wifi_Init();
        }
    }
}

void wifi_Init()
{
    byte tyres = 15;
    xSemaphoreTake(serial_Mutex, portMAX_DELAY);
    Serial.printf("\nWiFi ssid  %s\n", ssid_0);
    WiFi.disconnect();
    WiFi.setAutoConnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid_0, pass_0);
    while (--tyres && WiFi.status() != WL_CONNECTED)
    {
        vTaskDelay(500 / portTICK_PERIOD_MS);
        Serial.print(".");
    }
    Serial.println("NOK");
    while (WiFi.status() != WL_CONNECTED)
    {
        tyres = 15;
        Serial.printf("\nWiFi ssid  %s\n", ssid_0);
        WiFi.disconnect();
        WiFi.setAutoConnect(true);
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid_0, pass_0);
        while (--tyres && WiFi.status() != WL_CONNECTED)
        {
            vTaskDelay(500 / portTICK_PERIOD_MS);
            Serial.print(".");
        }
    }
    // Иначе удалось подключиться отправляем сообщение
    // о подключении и выводим адрес IP
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("IP address: ");
    Serial.print("ESP Mac Address: ");
    Serial.println(WiFi.macAddress());
    Serial.print("Subnet Mask: ");
    Serial.println(WiFi.subnetMask());
    Serial.print("Gateway IP: ");
    Serial.println(WiFi.gatewayIP());
    xSemaphoreGive(serial_Mutex);
}
void setup()
{
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector
    Serial.begin(115200);
    //Serial.setDebugOutput(true);
    Serial.println();
    // Create mutex before starting tasks
    serial_Mutex = xSemaphoreCreateMutex();
    camera_Semaphore = xSemaphoreCreateMutex();

    wifi_Init();

    xTaskCreatePinnedToCore(
        Task_WiFi_Code,     //Function to implement the task
        "Task_WiFi",        //Name of the task
        4096,               //Stack size in words
        NULL,               //Task input parameter
        11,                 //Priority of the task
        &task_WiFi_Handler, //Task handle.
        0                   //Core where the task should run
    );

    myBot.setUpdateTime(1000);
    myBot.setClock("EET-2EEST,M3.5.0/3,M10.5.0/4");
    myBot.setTelegramToken(token);

    // Check if all things are ok
    Serial.print("\nTest Telegram connection... ");
    if (myBot.begin() == true)
    {
        Serial.println("OK");
        getLocalTime(&timeinfo);
        Serial.println(&timeinfo, "\n%A, %B %d %Y %H:%M:%S");

        TBMessage msg;
        msg.sender.id = 387342374;
        myBot.sendMessage(msg, "MyBot ready.\nTry /reply_keyboard or /inline_keyboard");
    }
    else
    {
        Serial.println("NOK");
    }
    // Init and get the system time
    //configTime(3600, 0, "pool.ntp.org");
    //myBot.setClock("EET-2EEST,M3.5.0/3,M10.5.0/4");
    //getLocalTime(&timeinfo);
    xSemaphoreTake(serial_Mutex, portMAX_DELAY);
    //Serial.println(&timeinfo, "\n%A, %B %d %Y %H:%M:%S");

    // Init the camera
    Serial.print("Setup casmera ... ");
    cameraSetup(FRAMESIZE_UXGA); //QVGA|CIF|VGA|SVGA|XGA|SXGA

    if (SD_MMC_Setup())
    {
        Serial.println("OK!");
        Serial.printf("Total space: %10llu\n", SD_MMC.totalBytes());
        Serial.printf("Free space: %10llu\n", SD_MMC.totalBytes() - SD_MMC.usedBytes());
    }
    // Set the Telegram bot properies
    xSemaphoreGive(serial_Mutex);

    // Add reply keyboard
    isKeyboardActive = false;
    // add a button that send a message with "Simple button" text
    myReplyKbd.addButton("Take photo");
    myReplyKbd.addButton("Save photo");
    myReplyKbd.addButton("List dir SD");
    // add a new empty button row
    myReplyKbd.addRow();
    // add another button that send the user position (location)
    myReplyKbd.addButton("Send Location", KeyboardButtonLocation);
    // add another button that send the user contact
    myReplyKbd.addButton("Send contact", KeyboardButtonContact);
    // add a new empty button row
    myReplyKbd.addRow();
    // add a button that send a message with "Hide replyKeyboard" text
    // (it will be used to hide the reply keyboard)
    myReplyKbd.addButton("/hide_keyboard");
    // resize the keyboard to fit only the needed space
    myReplyKbd.enableResize();

    // Add sample inline keyboard
    myInlineKbd.addButton("ON", LIGHT_ON_CALLBACK, KeyboardButtonQuery);
    myInlineKbd.addButton("OFF", LIGHT_OFF_CALLBACK, KeyboardButtonQuery);
    myInlineKbd.addRow();
    myInlineKbd.addButton("GitHub", "https://github.com/cotestatnt/AsyncTelegram/", KeyboardButtonURL);

    //Start Task Message
    xTaskCreate(
        Task_Message_Code,      //Function to implement the task
        "Task_Message",         //Name of the task
        15000,                  //Stack size in words
        NULL,                   //Task input parameter
        12,                     //Priority of the task
        &task_Message_Handler); //Task handle.
                                //Start Task Picture
    xTaskCreate(
        Task_Picture_Code,      //Function to implement the task
        "Task_Picture",         //Name of the task
        15000,                  //Stack size in words
        NULL,                   //Task input parameter
        13,                     //Priority of the task
        &task_Picture_Handler); //Task handle.
}

void loop()
{
}
