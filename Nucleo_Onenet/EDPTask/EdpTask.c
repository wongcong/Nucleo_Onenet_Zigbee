#include <stdio.h>
#include <stdlib.h>
#if defined(_LINUX)
//#include <unistd.h>
//#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <getopt.h>
#include <libgen.h>
#endif
#include <string.h>

#include "EdpKit.h"
#include "stm32f4xx_hal.h"
#include "adc.h"
#include "usart.h"
#include "utils.h"
#include "EdpTask.h"
#include "esp8266.h"

int8_t src_api_key[] = "ZUrbpP=pZqT4zjDvVHpZdbuQDHw=";
int8_t src_dev[] = "20531004";
int8_t water_line = 30;
extern volatile uint8_t Uart6_Rxdata;
static man = 0;

/**
 * @brief  EDP数据包发送
 * @param  buffer: 要发送的数据缓冲区地址
* @param  len: 要发送的数据缓长度
* @param  sockfd：兼容linux socket api: STM32下无意义
 * @retval 发送的数据长度
 **/
int32_t DoSend(int32_t sockfd, const uint8_t *buffer, uint32_t len)
{
	HAL_UART_Transmit(&huart1, (uint8_t*)buffer, len, len * 100);
	HAL_Delay(1000);
	/* wululu test print send bytes */
	hexdump((const uint8_t *)buffer, len);
	return len;
}
/*
 *  @brief  EDP协议向自己透传数据，用于测试，将src_dev替换成目标DEVICE ID即可
 */
void Push_DataToMyself(void)
{
	EdpPacket* send_pkg;
	int8_t push_data[] = { 44 };
	printf("%s %d\n", __func__, __LINE__);
	send_pkg = PacketPushdata(src_dev, push_data, sizeof(push_data));
	DoSend(0, (const uint8_t *)send_pkg->_data, send_pkg->_write_pos);
	DeleteBuffer(&send_pkg);
	HAL_Delay(1000);
}

/*
 *  @brief  EDP协议向Onenet上传湿度信息，数据点格式TYPE=3
 */
void Save_HumidityToOneNet(void)
{
	uint16_t humidity;
	uint16_t AD_Value = 0;
	HAL_ADC_Start(&hadc1);
	HAL_ADC_PollForConversion(&hadc1, 50);
	if (HAL_IS_BIT_SET(HAL_ADC_GetState(&hadc1), HAL_ADC_STATE_REG_EOC))
	{
		AD_Value = HAL_ADC_GetValue(&hadc1);
		humidity = (AD_Value * 100) / 4100;
	}
	printf("%s %d humidity:%d\n", __func__, __LINE__, (uint32_t)humidity);
	//构造一个包
	cJSON *json_data = cJSON_CreateObject();
	cJSON_AddNumberToObject(json_data, "humidity", humidity);
	EdpPacket* send_pkg = PacketSavedataJson((int8_t *)src_dev, json_data, kTypeSimpleJsonWithoutTime);

	DoSend(0, (const uint8_t *)send_pkg->_data, send_pkg->_write_pos);
	DeleteBuffer(&send_pkg);
	/*删除构造的json对象*/
	cJSON_Delete(json_data);
	HAL_Delay(1000);
}

/*
*  @brief  浇花,少量多次
*/
void Water_Flower(uint8_t iscmd)
{
	if (man == 1)
	{
		if (iscmd == 0)
			return;
	}

	if (iscmd == 1)
	{
		PrintLog("Manual water flower, open\r\n");
		//命令强制浇水
		HAL_GPIO_WritePin(Motor_GPIO_Port, Motor_Pin, GPIO_PIN_SET);
		return;
	}
	else if (iscmd == 2)
	{
		PrintLog("Manual water flower, close\r\n");
		//命令强制关
		HAL_GPIO_WritePin(Motor_GPIO_Port, Motor_Pin, GPIO_PIN_RESET);
		return;
	}

	PrintLog("Auto water flower\r\n");
	//使能继电器1s
	HAL_GPIO_WritePin(Motor_GPIO_Port, Motor_Pin, GPIO_PIN_SET);
	HAL_Delay(1000);
	HAL_GPIO_WritePin(Motor_GPIO_Port, Motor_Pin, GPIO_PIN_RESET);
}

/*
*  @brief  补光，正式使用时使用注释行
*/
void Fill_Light(uint8_t status, uint8_t iscmd)
{
	if (man == 1)
	{
		if (iscmd == 0)
			return;
		else PrintLog("Manual ");
	}
	else
	{
		PrintLog("Auto ");
	}

	if (status == 1)
	{
		PrintLog("fill light, close\r\n");
		//有光关灯
		HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
		//HAL_GPIO_WritePin(Light_GPIO_Port, Light_Pin, GPIO_PIN_RESET);
	}
	else
	{
		PrintLog("fill light, open\r\n");
		//无光开灯
		HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
		//HAL_GPIO_WritePin(Light_GPIO_Port, Light_Pin, GPIO_PIN_SET);
	}
}

/*
*  @brief  EDP协议向Onenet上传信息，数据点格式TYPE=3
*/
void Save_AllSensorsToOneNet(void)
{
	uint16_t humidity;
	uint16_t zigbee_status;
	uint16_t AD_Value = 0;

	//获取湿度传感器信息的值
	HAL_ADC_Start(&hadc1);
	HAL_ADC_PollForConversion(&hadc1, 50);
	if (HAL_IS_BIT_SET(HAL_ADC_GetState(&hadc1), HAL_ADC_STATE_REG_EOC))
	{
		AD_Value = HAL_ADC_GetValue(&hadc1);
		humidity = 100 - (AD_Value / 41);
	}
	printf("%s %d humidity:%d\n", __func__, __LINE__, (uint32_t)humidity);
	//如果湿度低于设定值则浇水
	if (humidity < water_line)
	{
		PrintLog("Drought\r\n");
		Water_Flower(0);
	}
	else
	{
		PrintLog("Wet\r\n");
	}

	//读取zigbee的串口信息
	if (Uart6_Rxdata == '1')
	{
		PrintLog("Light\r\n");
		zigbee_status = 1;
	}
	else
	{
		PrintLog("Dark\r\n");
		zigbee_status = 0;
	}
	Fill_Light(zigbee_status, 0);

	//构造一个包
	cJSON *json_data = cJSON_CreateObject();
	cJSON_AddNumberToObject(json_data, "humidity", humidity);
	cJSON_AddNumberToObject(json_data, "zigbee", zigbee_status);
	EdpPacket* send_pkg = PacketSavedataJson((int8_t *)src_dev, json_data, kTypeSimpleJsonWithoutTime);
	//发送
	DoSend(0, (const uint8_t *)send_pkg->_data, send_pkg->_write_pos);
	DeleteBuffer(&send_pkg);
	/*删除构造的json对象*/
	cJSON_Delete(json_data);
	HAL_Delay(1000);
}
/*
 *  @brief  EDP协议测试主循环
 */
void EDP_Loop(void)
{

	HAL_Delay(2000);
	while (1)
	{
		//连接onenet
		Connect_RequestType1(src_dev, src_api_key);
		HAL_Delay(3000);
		Recv_Thread_Func();

		printf("%s %d\n", __func__, __LINE__);
		HAL_Delay(5000);
		//储存数据到onenet
		Save_AllSensorsToOneNet();
		HAL_Delay(3000);
		Recv_Thread_Func();

		printf("%s %d\n", __func__, __LINE__);
		HAL_Delay(5000);
		////接收命令
		//Recv_Thread_Func();
		//printf("%s %d\n", __func__, __LINE__);
		//HAL_Delay(3000);
	}
}

/*
 *  @brief  串口接收处理线程
 */
void Recv_Thread_Func(void)
{
	int32_t error = 0;
	int32_t rtn;
	uint8_t mtype, jsonorbin;
	uint8_t buffer[128];
	RecvBuffer *recv_buf = NewBuffer();
	EdpPacket *pkg;

	int8_t *src_devid;
	int8_t *push_data;
	uint32_t push_datalen;

	cJSON *desc_json;
	int8_t *desc_json_str;
	int8_t *save_bin;
	uint32_t save_binlen;
	int8_t *json_ack;

	int8_t *cmdid;
	uint16_t cmdid_len;
	int8_t *cmd_req;
	uint32_t cmd_req_len;
	EdpPacket *send_pkg;

	cJSON *save_json;
	int8_t *save_json_str;
	//int8_t *ds_id;
	//int8_t iValue = 0;

	int8_t *simple_str = NULL;
	int8_t cmd_resp[] = "ok";
	uint32_t cmd_resp_len = 0;

	printf("\n[%s] recv thread start ...\r\n", __func__);

	while (error == 0)
	{
		/* 试着接收1024个字节的数据 */
		int32_t rcv_len = 0;

		rcv_len = USART2_GetRcvNum();
		if (rcv_len <= 0)
		{
			printf("%s %d No Data\n", __func__, __LINE__);
			break;
		}
		HAL_Delay(50);
		rcv_len = USART2_GetRcvNum();
		USART2_GetRcvData(buffer, rcv_len);
		printf("recv from server, bytes: %d\r\n", rcv_len);
		/* wululu test print send bytes */
		hexdump((const uint8_t *)buffer, rcv_len);
		printf("\n");
		/* 成功接收了n个字节的数据 */
		WriteBytes(recv_buf, buffer, rcv_len);
		while (1)
		{
			/* 获取一个完成的EDP包 */
			if ((pkg = GetEdpPacket(recv_buf)) == 0)
			{
				printf("need more bytes...\n");
				break;
			}
			/* 获取这个EDP包的消息类型 */
			mtype = EdpPacketType(pkg);
			printf("mtype=%d\n", mtype);
			/* 根据这个EDP包的消息类型, 分别做EDP包解析 */
			switch (mtype)
			{
			case CONNRESP:
				/* 解析EDP包 - 连接响应 */
				rtn = UnpackConnectResp(pkg);
				printf("recv connect resp, rtn: %d\n", rtn);
				break;
			case PUSHDATA:
				/* 解析EDP包 - 数据转发 */
				UnpackPushdata(pkg, &src_devid, &push_data,
					&push_datalen);
				printf
				("recv push data, src_devid: %s, push_data: %s, len: %d\n",
					src_devid, push_data, push_datalen);
				free(src_devid);
				free(push_data);
				break;
			case SAVEDATA:
				/* 解析EDP包 - 数据存储 */
				if (UnpackSavedata(pkg, &src_devid, &jsonorbin)
					== 0)
				{
					if (jsonorbin == kTypeFullJson
						|| jsonorbin ==
						kTypeSimpleJsonWithoutTime
						|| jsonorbin ==
						kTypeSimpleJsonWithTime)
					{
						printf("json type is %d\n",
							jsonorbin);
						/* 解析EDP包 - json数据存储 */
						UnpackSavedataJson(pkg, &save_json);
						save_json_str = cJSON_Print(save_json);
						printf("recv save data json, src_devid: %s, json: %s\n",
							src_devid, save_json_str);
						free(save_json_str);
						cJSON_Delete(save_json);

						//UnpackSavedataInt(jsonorbin, pkg, (char **)&ds_id, &iValue);
						//printf("ds_id = %s\nvalue= %d\n", ds_id, iValue); 

						//UnpackSavedataDouble((SaveDataType)jsonorbin,
						//	pkg,
						//	(char **)&ds_id,
						//	&dValue);
						//printf
						//("ds_id = %s\nvalue = %f\n",
						//	ds_id, dValue);

						//UnpackSavedataString(jsonorbin, pkg, &ds_id, &cValue);
						//printf("ds_id = %s\nvalue = %s\n", ds_id, cValue);
						//free(cValue);
						//free(ds_id);
					}
					else if (jsonorbin == kTypeBin)     /* 解析EDP包 - bin数据存储 */
					{
						UnpackSavedataBin(pkg,
							&desc_json,
							(uint8_t **)&
							save_bin,
							&save_binlen);
						desc_json_str =
							(int8_t *)cJSON_Print(desc_json);
						printf
						("recv save data bin, src_devid: %s, desc json: %s, bin: %s, binlen: %d\n",
							src_devid, desc_json_str,
							save_bin, save_binlen);
						free(desc_json_str);
						cJSON_Delete(desc_json);
						free(save_bin);
					}
					else if (jsonorbin == kTypeString)
					{
						UnpackSavedataSimpleString(pkg,
							&simple_str);
						printf("%s\n", simple_str);
						free(simple_str);
					}
					free(src_devid);
				}
				else
				{
					printf("error\n");
				}
				break;
			case SAVEACK:
				json_ack = NULL;
				UnpackSavedataAck(pkg, &json_ack);
				printf("save json ack = %s\n", json_ack);
				free(json_ack);
				break;
			case CMDREQ:
				if (UnpackCmdReq(pkg, &cmdid, &cmdid_len, &cmd_req, &cmd_req_len) == 0)
				{
					//PrintLog_l(cmd_req, cmd_req_len);
					switch (*cmd_req)
					{
					case '0':
						//auto
						PrintLog("Auto mode\r\n");
						man = 0;
						break;
					case '1':
						//manual
						PrintLog("Manuals mode\r\n");
						man = 1;
						break;
					case '2':
						//start water
						PrintLog("Stop water\r\n");
						Water_Flower(2);
						break;
					case '3':
						//stop water
						PrintLog("Start water\r\n");
						Water_Flower(1);
						break;
					case '4':
						//close light
						PrintLog("Close light\r\n");
						Fill_Light(1, 1);
						break;
					case '5':
						//open light
						PrintLog("Open light\r\n");
						Fill_Light(0, 1);
						break;
					case '6':
						//zigbee off
						PrintLog("zigbee = 0\r\n");
						break;
					case '7':
						//zigbee on
						PrintLog("zigbee = 1\r\n");
						break;
					default:
						break;
					}
					/*
					 * 用户按照自己的需求处理并返回，响应消息体可以为空，此处假设返回2个字符"ok"。
					 * 处理完后需要释放
					 */
					//cmd_resp_len = strlen((const char *)cmd_resp);
					//send_pkg = PacketCmdResp(cmdid, cmdid_len, cmd_resp, cmd_resp_len);
//#ifdef _ENCRYPT
//					if (g_is_encrypt)
//					{
//						SymmEncrypt(send_pkg);
//					}
//#endif
//					DoSend(0, (const uint8_t *)
//						send_pkg->_data,
//						send_pkg->_write_pos);
//					DeleteBuffer(&send_pkg);

					free(cmdid);
					free(cmd_req);
				}
				break;
			case PINGRESP:
				/* 解析EDP包 - 心跳响应 */
				UnpackPingResp(pkg);
				printf("recv ping resp\n");
				break;
			default:
				/* 未知消息类型 */
				error = 1;
				printf("recv failed...\n");
				break;
			}
			DeleteBuffer(&pkg);
		}
	}
	DeleteBuffer(&recv_buf);

#ifdef _DEBUG
	printf("[%s] recv thread end ...\n", __func__);
#endif
}

void Connect_RequestType1(int8_t *devid, int8_t *api_key)
{
	EdpPacket *send_pkg;

	send_pkg = PacketConnect1((const int8_t *)devid, (const int8_t *)api_key);
	if (send_pkg == NULL)
	{
		return;
	}
	/* send_pkg = PacketConnect2("433223", "{ \"SYS\" : \"0DEiuApATHgLurKNEl6vY4bLwbQ=\" }"); */
	/* send_pkg = PacketConnect2("433223", "{ \"13982031959\" : \"888888\" }"); */

	/* 向设备云发送连接请求 */
	printf("send connect to server, bytes: %d\n", send_pkg->_write_pos);
	DoSend(0, (const uint8_t *)send_pkg->_data, send_pkg->_write_pos);
	/* 必须释放这个内存，否则造成泄露 */
	DeleteBuffer(&send_pkg);
	HAL_Delay(1000);
}
/*
 *  @brief  发送PING包维持心跳
 */
void Ping_Server(void)
{
	EdpPacket *send_pkg;
	printf("%s %d\n", __func__, __LINE__);
	/* 组装ping包 */
	send_pkg = PacketPing();

	DoSend(0, (const uint8_t *)send_pkg->_data,
		send_pkg->_write_pos);
	HAL_Delay(500);
	/* 必须释放这个内存，否则造成泄露 */
	DeleteBuffer(&send_pkg);
	HAL_Delay(100);
}
