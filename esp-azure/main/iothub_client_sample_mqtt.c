// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdio.h>
#include <stdlib.h>

#include "esp_system.h"

#include "iothub_client.h"
#include "iothub_message.h"
#include "azure_c_shared_utility/threadapi.h"
#include "azure_c_shared_utility/crt_abstractions.h"
#include "azure_c_shared_utility/platform.h"
#include "iothubtransportmqtt.h"
#include "iothub_client_version.h"
#include "iothub_device_client_ll.h"
#include "iothub_client_options.h"
#include "azure_prov_client/prov_device_ll_client.h"
#include "azure_prov_client/prov_security_factory.h"
#include "azure_prov_client/prov_transport_mqtt_client.h"

#ifdef MBED_BUILD_TIMESTAMP
#include "certs.h"
#endif // MBED_BUILD_TIMESTAMP

static int callbackCounter;
static char msgText[1024];
static char propText[1024];
static bool g_continueRunning;

#define MESSAGE_COUNT 0 // Number of telemetry messages to send before exiting, or 0 to keep sending forever
#define DOWORK_LOOP_NUM     3


typedef struct EVENT_INSTANCE_TAG
{
    IOTHUB_MESSAGE_HANDLE messageHandle;
    size_t messageTrackingId;  // For tracking the messages within the user callback.
} EVENT_INSTANCE;


DEFINE_ENUM_STRINGS(PROV_DEVICE_RESULT, PROV_DEVICE_RESULT_VALUE);
DEFINE_ENUM_STRINGS(PROV_DEVICE_REG_STATUS, PROV_DEVICE_REG_STATUS_VALUES);

static const char* global_prov_uri = "global.azure-devices-provisioning.net";
static char* g_access_key = "PRIMARY OR SECONDARY KEY HERE";
static const char* scope_id = "SCOPE ID HERE";
static const char* reg_id = "DEVICE ID HERE";
static char* conn_str = NULL;

static bool g_use_proxy = false;
static const char* PROXY_ADDRESS = "127.0.0.1";

#define PROXY_PORT                  8888
#define MESSAGES_TO_SEND            2
#define TIME_BETWEEN_MESSAGES       2

typedef struct CLIENT_SAMPLE_INFO_TAG
{
    unsigned int sleep_time;
    char* iothub_uri;
    char* access_key_name;
    char* device_key;
    char* device_id;
    int registration_complete;
} CLIENT_SAMPLE_INFO;

typedef struct IOTHUB_CLIENT_SAMPLE_INFO_TAG
{
    int connected;
    int stop_running;
} IOTHUB_CLIENT_SAMPLE_INFO;

static IOTHUBMESSAGE_DISPOSITION_RESULT receive_msg_callback(IOTHUB_MESSAGE_HANDLE message, void* user_context)
{
    IOTHUB_CLIENT_SAMPLE_INFO* iothub_info = (IOTHUB_CLIENT_SAMPLE_INFO*)user_context;
   printf("Stop message recieved from IoTHub\r\n");
    iothub_info->stop_running = 1;
    return IOTHUBMESSAGE_ACCEPTED;
}

static void registation_status_callback(PROV_DEVICE_REG_STATUS reg_status, void* user_context)
{
   printf(".");
}

static void iothub_connection_status(IOTHUB_CLIENT_CONNECTION_STATUS result, IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason, void* user_context)
{
    if (user_context == NULL)
    {
        printf("iothub_connection_status user_context is NULL\r\n");
    }
    else
    {
        IOTHUB_CLIENT_SAMPLE_INFO* iothub_info = (IOTHUB_CLIENT_SAMPLE_INFO*)user_context;
        if (result == IOTHUB_CLIENT_CONNECTION_AUTHENTICATED)
        {
            iothub_info->connected = 1;
        }
        else
        {
            iothub_info->connected = 0;
            iothub_info->stop_running = 1;
        }
    }
}

static void register_device_callback(PROV_DEVICE_RESULT register_result, const char* iothub_uri, const char* device_id, void* user_context)
{
    if (user_context == NULL)
    {
        printf("user_context is NULL\r\n");
    }
    else
    {
        CLIENT_SAMPLE_INFO* user_ctx = (CLIENT_SAMPLE_INFO*)user_context;
        if (register_result == PROV_DEVICE_RESULT_OK)
        {
           printf("\nRegistration Information received from service: %s!\r\n", iothub_uri);
           mallocAndStrcpy_s(&user_ctx->iothub_uri, iothub_uri);
           mallocAndStrcpy_s(&user_ctx->device_id, device_id);
            user_ctx->registration_complete = 1;

            size_t len = snprintf(NULL, 0,
                "HostName=%s;DeviceId=%s;SharedAccessKey=%s",
                iothub_uri,
                device_id,
                g_access_key);
            conn_str = (char*) malloc(len + 2);
            if (conn_str == NULL) {
                printf("OOM while creating connection string\n");
                exit(1);
            }

            int pos = snprintf(conn_str, len + 1,
                "HostName=%s;DeviceId=%s;SharedAccessKey=%s",
                iothub_uri,
                device_id,
                g_access_key);
            conn_str[pos] = 0;
        }
        else
        {
           printf("Failure encountered on registration %s\r\n", ENUM_TO_STRING(PROV_DEVICE_RESULT, register_result) );
            user_ctx->registration_complete = 2;
        }
    }
}

bool getConnectionString() {
    prov_dev_set_symmetric_key_info(reg_id, g_access_key);

    bool traceOn = false;

   prov_dev_security_init(SECURE_DEVICE_TYPE_SYMMETRIC_KEY);
    HTTP_PROXY_OPTIONS http_proxy;
    CLIENT_SAMPLE_INFO user_ctx;

    memset(&http_proxy, 0, sizeof(HTTP_PROXY_OPTIONS));
    memset(&user_ctx, 0, sizeof(CLIENT_SAMPLE_INFO));

    user_ctx.registration_complete = 0;
    user_ctx.sleep_time = 10;

    if (g_use_proxy)
    {
        http_proxy.host_address = PROXY_ADDRESS;
        http_proxy.port = PROXY_PORT;
    }

    PROV_DEVICE_LL_HANDLE handle;
    if ((handle = Prov_Device_LL_Create(global_prov_uri, scope_id, Prov_Device_MQTT_Protocol)) == NULL)
    {
       printf("failed calling Prov_Device_LL_Create\r\n");
    }
    else
    {
        if (http_proxy.host_address != NULL)
        {
            Prov_Device_LL_SetOption(handle, OPTION_HTTP_PROXY, &http_proxy);
        }

        Prov_Device_LL_SetOption(handle, PROV_OPTION_LOG_TRACE, &traceOn);
        if (Prov_Device_LL_Register_Device(handle, register_device_callback, &user_ctx, registation_status_callback, &user_ctx) != PROV_DEVICE_RESULT_OK)
        {
           printf("failed calling Prov_Device_LL_Register_Device\r\n");
        }
        else
        {
            do
            {
                Prov_Device_LL_DoWork(handle);
                ThreadAPI_Sleep(user_ctx.sleep_time);
            } while (user_ctx.registration_complete == 0);
        }
        Prov_Device_LL_Destroy(handle);
    }

    if (user_ctx.registration_complete != 1)
    {
       printf("error: registration failed!\r\n");
    }
    else
    {
        IOTHUB_CLIENT_TRANSPORT_PROVIDER iothub_transport;
        iothub_transport = MQTT_Protocol;
        IOTHUB_DEVICE_CLIENT_LL_HANDLE device_ll_handle;

        if ((device_ll_handle = IoTHubDeviceClient_LL_CreateFromDeviceAuth(user_ctx.iothub_uri, user_ctx.device_id, iothub_transport) ) == NULL)
        {
           printf("failed create IoTHub client from connection string %s!\r\n", user_ctx.iothub_uri);
           return false;
        }
        else
        {
            IOTHUB_CLIENT_SAMPLE_INFO iothub_info;
            iothub_info.stop_running = 0;
            iothub_info.connected = 0;

           IoTHubDeviceClient_LL_SetConnectionStatusCallback(device_ll_handle, iothub_connection_status, &iothub_info);
            IoTHubDeviceClient_LL_Destroy(device_ll_handle);
        }
    }
    free(user_ctx.iothub_uri);
    free(user_ctx.device_id);

    return true;
}

static unsigned char* bytearray_to_str(const unsigned char *buffer, size_t len)
{
    unsigned char* ret = (unsigned char*)malloc(len+1);
    memcpy(ret, buffer, len);
    ret[len] = '\0';
    return ret;
}

static IOTHUBMESSAGE_DISPOSITION_RESULT ReceiveMessageCallback(IOTHUB_MESSAGE_HANDLE message, void* userContextCallback)
{

    int* counter = (int*)userContextCallback;
    const char* buffer;
    size_t size;

    if (IoTHubMessage_GetByteArray(message, (const unsigned char**)&buffer, &size) != IOTHUB_MESSAGE_OK)
    {
       printf("unable to retrieve the message data\r\n");
    }
    else
    {
        unsigned char* message_string = bytearray_to_str((const unsigned char *)buffer, size);
       printf("IoTHubMessage_GetByteArray received message: \"%s\" \n", message_string);
        free(message_string);

        // If we receive the word 'quit' then we stop running
        if (size == (strlen("quit") * sizeof(char)) && memcmp(buffer, "quit", size) == 0)
        {
            g_continueRunning = false;
        }
    }

    // Retrieve properties from the message
    MAP_HANDLE mapProperties = IoTHubMessage_Properties(message);
    if (mapProperties != NULL)
    {
        const char*const* keys;
        const char*const* values;
        size_t propertyCount = 0;
        if (Map_GetInternals(mapProperties, &keys, &values, &propertyCount) == MAP_OK)
        {
            if (propertyCount > 0)
            {
                size_t index = 0;
                for (index = 0; index < propertyCount; index++)
                {
                    //(void)printf("\tKey: %s Value: %s\r\n", keys[index], values[index]);
                }
                //(void)printf("\r\n");
            }
        }
    }

    /* Some device specific action code goes here... */
    (*counter)++;
    return IOTHUBMESSAGE_ACCEPTED;
}

static void SendConfirmationCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void* userContextCallback)
{
    EVENT_INSTANCE* eventInstance = (EVENT_INSTANCE*)userContextCallback;
    size_t id = eventInstance->messageTrackingId;

   printf("Confirmation[%d] received for message tracking id = %d with result = %s\r\n", callbackCounter, (int)id, ENUM_TO_STRING(IOTHUB_CLIENT_CONFIRMATION_RESULT, result));
    /* Some device specific action code goes here... */
    callbackCounter++;
    IoTHubMessage_Destroy(eventInstance->messageHandle);
}

void iothub_client_sample_mqtt_run(void)
{
	printf("\nFile:%s Compile Time:%s %s\n",__FILE__,__DATE__,__TIME__);
    IOTHUB_CLIENT_LL_HANDLE iotHubClientHandle;

    EVENT_INSTANCE messages[MESSAGE_COUNT || 1];

    g_continueRunning = true;
    srand((unsigned int)time(NULL));
    double avgWindSpeed = 10.0;

    callbackCounter = 0;
    int receiveContext = 0;
    if (platform_init() != 0)
    {
       printf("Failed to initialize the platform.\r\n");
    }
    else
    {
        if (!getConnectionString()) return;
        if ((iotHubClientHandle = IoTHubClient_LL_CreateFromConnectionString(conn_str, MQTT_Protocol)) == NULL)
        {
           printf("ERROR: iotHubClientHandle is NULL!\r\n");
        }
        else
        {
            bool traceOn = true;
            IoTHubClient_LL_SetOption(iotHubClientHandle, "logtrace", &traceOn);

#ifdef MBED_BUILD_TIMESTAMP
            // For mbed add the certificate information
            if (IoTHubClient_LL_SetOption(iotHubClientHandle, "TrustedCerts", certificates) != IOTHUB_CLIENT_OK)
            {
                printf("failure to set option \"TrustedCerts\"\r\n");
            }
#endif // MBED_BUILD_TIMESTAMP

            /* Setting Message call back, so we can receive Commands. */
            if (IoTHubClient_LL_SetMessageCallback(iotHubClientHandle, ReceiveMessageCallback, &receiveContext) != IOTHUB_CLIENT_OK)
            {
               printf("ERROR: IoTHubClient_LL_SetMessageCallback..........FAILED!\r\n");
            }
            else
            {
               printf("IoTHubClient_LL_SetMessageCallback...successful.\r\n");

                /* Now that we are ready to receive commands, let's send some messages */
                size_t iterator = 0;

                do
                {
                    if ((!MESSAGE_COUNT || (iterator < MESSAGE_COUNT)) && (iterator<= callbackCounter))
                    {
                        EVENT_INSTANCE *thisMessage = &messages[MESSAGE_COUNT ? iterator : 0];

                        sprintf_s(msgText, sizeof(msgText), "{\"deviceId\":\"AirConditionDevice_001\",\"windSpeed\":%.2f}", avgWindSpeed + (rand() % 4 + 2));
                        printf("Ready to Send String:%s\n",(const char*)msgText);
                        if ((thisMessage->messageHandle = IoTHubMessage_CreateFromByteArray((const unsigned char*)msgText, strlen(msgText))) == NULL)
                        {
                           printf("ERROR: iotHubMessageHandle is NULL!\r\n");
                        }
                        else
                        {
                            thisMessage->messageTrackingId = iterator;
                            MAP_HANDLE propMap = IoTHubMessage_Properties(thisMessage->messageHandle);
                           sprintf_s(propText, sizeof(propText), "PropMsg_%zu", iterator);
                            if (Map_AddOrUpdate(propMap, "PropName", propText) != MAP_OK)
                            {
                               printf("ERROR: Map_AddOrUpdate Failed!\r\n");
                            }
                            if (IoTHubClient_LL_SendEventAsync(iotHubClientHandle, thisMessage->messageHandle, SendConfirmationCallback, thisMessage) != IOTHUB_CLIENT_OK)
                            {
                               printf("ERROR: IoTHubClient_LL_SendEventAsync..........FAILED!\r\n");
                            }
                            else
                            {
                               printf("IoTHubClient_LL_SendEventAsync accepted message [%d] for transmission to IoT Hub.\r\n", (int)iterator);
                            }
                        }
                        iterator++;
                    }
                    IoTHubClient_LL_DoWork(iotHubClientHandle);
                    printf("Sleeping for 5\n");
                    ThreadAPI_Sleep(5000);

                    // if (callbackCounter>=MESSAGE_COUNT){
                    //     printf("done sending...\n");
                    //     break;
                    // }
                } while (g_continueRunning);

               printf("iothub_client_sample_mqtt has gotten quit message, call DoWork %d more time to complete final sending...\r\n", DOWORK_LOOP_NUM);
                size_t index = 0;
                for (index = 0; index < DOWORK_LOOP_NUM; index++)
                {
                    IoTHubClient_LL_DoWork(iotHubClientHandle);
                    ThreadAPI_Sleep(1);
                }
            }
            IoTHubClient_LL_Destroy(iotHubClientHandle);
        }
        platform_deinit();
    }
}

int main(void)
{
    iothub_client_sample_mqtt_run();
    return 0;
}
