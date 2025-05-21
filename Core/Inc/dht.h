#ifndef DHT_H_
#define DHT_H_

#include "stdio.h"

typedef struct {
	float Temperature;
	float Humidity;
} DHT_DataTypedef;

void DHT_GetData(DHT_DataTypedef *DHT_Data);
void DHT22_Start(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);
uint8_t DHT22_Check_Response(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);
uint8_t DHT22_Read(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);


#endif /* INC_DHT_H_ */
