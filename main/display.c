#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/i2c.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "app.h"
#define W 240
#define H 320
static uint16_t *frame,*stripe;
static esp_lcd_panel_handle_t panel;
static SemaphoreHandle_t done;
static bool lcd_ok=false,tp_ok=false;
static int glitch_count;
bool display_ready(void){return lcd_ok;}
bool touch_ready(void){return tp_ok;}
static inline uint16_t rgb(uint8_t r,uint8_t g,uint8_t b){uint16_t c=((r&248)<<8)|((g&252)<<3)|(b>>3);return (c>>8)|(c<<8);}
static void rect(int x,int y,int w,int h,uint16_t c){for(int yy=y;yy<y+h&&yy<H;yy++)for(int xx=x;xx<x+w&&xx<W;xx++)if(xx>=0&&yy>=0)frame[yy*W+xx]=c;}
#include "font.inc"
static void tc(int y,const char*s,int z,uint16_t c){text((W-((int)strlen(s)*6*z-z))/2,y,s,z,c);}
static bool finished(esp_lcd_panel_io_handle_t io,esp_lcd_panel_io_event_data_t*ev,void*ctx){BaseType_t wake=pdFALSE;xSemaphoreGiveFromISR(done,&wake);return wake==pdTRUE;}
static void show(void){
 for(int y=0;y<H;y+=16){memcpy(stripe,frame+y*W,W*16*2);
 if(esp_lcd_panel_draw_bitmap(panel,0,y,W,y+16,stripe)!=ESP_OK){lcd_ok=false;return;}
 if(xSemaphoreTake(done,pdMS_TO_TICKS(1000))!=pdTRUE){lcd_ok=false;ESP_LOGE("LCD","DMA timeout");vTaskDelete(NULL);}
 }
}
static void draw(int page,int phase){
 uint16_t wh=rgb(240,245,235),accent=rgb(255,191,69),dim=rgb(77,88,94),ok=rgb(137,239,194);rect(0,0,W,H,rgb(7,13,18));
 if(page==0){
  tc(58,"PRISMA P",4,wh);uint16_t pal[]={rgb(255,191,69),rgb(137,239,194),rgb(250,246,220),rgb(196,142,84),rgb(100,185,168)};
  tc(110,"CAMERA",3,pal[phase%5]);rect(42,155,156,2,accent);tc(184,"TAP FOR GLITCH",1,wh);tc(214,"SWIPE TO CONTINUE",1,dim);tc(255,"BUILD 003",1,accent);
 }else if(page==1){
  char sensor[40],pid[20],fr[32],sz[32],tm[32];
  tc(18,"PRISMA P",3,wh);tc(50,"CAMERA + FRAME TEST",1,accent);
  tc(82,camera_ready?"CAMERA READY":camera_probed?"CAMERA ERROR":"CAMERA PROBING",2,camera_ready?ok:accent);
  snprintf(sensor,sizeof(sensor),"SENSOR %s",camera_sensor_name);tc(122,sensor,1,camera_ready?wh:dim);
  snprintf(pid,sizeof(pid),"PID %04X",camera_pid);tc(143,pid,1,camera_ready?wh:dim);
  tc(168,camera_capture_ready?"FRAME CAPTURED":camera_capture_attempted?"CAPTURE ERROR":"CAPTURE PENDING",1,camera_capture_ready?ok:accent);
  if(camera_capture_ready){
   snprintf(fr,sizeof(fr),"FRAME %ux%u %s",camera_frame_width,camera_frame_height,camera_frame_format);tc(193,fr,1,wh);
   snprintf(sz,sizeof(sz),"SIZE %u BYTES",(unsigned)camera_frame_bytes);tc(215,sz,1,wh);
   snprintf(tm,sizeof(tm),"CAPTURE %u MS",(unsigned)camera_capture_ms);tc(237,tm,1,wh);
  }else tc(215,"STREAM PENDING",1,dim);
 }else{
  char cam[40];
  tc(17,"ADMINISTRATION",2,accent);tc(59,network_ready?"WIFI AP ACTIVE":"NETWORK ERROR",1,wh);tc(87,app_ssid,1,wh);tc(116,"WIFI PASSWORD",1,dim);tc(138,app_password,2,accent);tc(184,network_ready?"192.168.4.1":"CHECK USB LOG",2,wh);
  snprintf(cam,sizeof(cam),"CAMERA %s",camera_ready?camera_sensor_name:camera_probed?"ERROR":"PROBING");tc(224,cam,1,camera_ready?ok:dim);
  tc(246,camera_capture_ready?"FRAME TEST READY":camera_capture_attempted?"FRAME TEST ERROR":"FRAME TEST PENDING",1,camera_capture_ready?ok:dim);tc(268,"MDNS PENDING",1,dim);
 }
 for(int i=0;i<3;i++){rect(96+i*20,297,8,8,i==page?accent:dim);}show();
}
static void glitch(int x,int y){
 uint16_t pal[]={rgb(255,191,69),rgb(137,239,194),rgb(250,246,220),rgb(196,142,84),rgb(100,185,168)};
 for(int i=0;i<9;i++){int dx=((i*17+glitch_count*11)%55)-27,dy=((i*23+glitch_count*7)%55)-27,sz=4+((i+glitch_count)%4)*3;rect(x+dx,y+dy,sz,sz,pal[(i+glitch_count)%5]);}glitch_count++;show();
}
static esp_err_t read_touch(bool *pressed,int*x,int*y){uint8_t reg=2,b[5];esp_err_t e=i2c_master_write_read_device(I2C_NUM_0,0x15,&reg,1,b,5,pdMS_TO_TICKS(30));if(e!=ESP_OK)return e;*pressed=(b[0]&15)>0;*x=((b[1]&15)<<8)|b[2];*y=((b[3]&15)<<8)|b[4];if(*x>=W||*y>=H)*pressed=false;return ESP_OK;}
static void ui_task(void*unused){
 int page=0,phase=0,anim=0,cooldown=0,miss=0,sx=0,sy=0;bool down=false,wait_release=false;
 draw(page,phase);gpio_set_level(1,1);
 while(1){if(cooldown>0)cooldown--;bool touched=false;int x=0,y=0;
  esp_err_t e=tp_ok?read_touch(&touched,&x,&y):ESP_FAIL;
  if(e==ESP_OK){
   if(wait_release){if(!touched)wait_release=false;}
   else if(touched){miss=0;if(!down){sx=x;sy=y;down=true;}else{int dx=x-sx;
    if((dx<-55&&page<2)||(dx>55&&page>0)){page+=dx<0?1:-1;glitch_count=0;draw(page,phase);down=false;wait_release=true;cooldown=25;}
   }}else if(down&&++miss>4){if(page==0&&cooldown==0)glitch(sx,sy);down=false;miss=0;}
  }
  if(page==0&&glitch_count==0&&++anim>=6){anim=0;phase=(phase+1)%5;draw(page,phase);}vTaskDelay(pdMS_TO_TICKS(20));
 }
}
#define TRY(expr) do{esp_err_t e=(expr);if(e!=ESP_OK)return e;}while(0)
esp_err_t display_start(void){
 frame=heap_caps_malloc(W*H*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!frame)frame=heap_caps_malloc(W*H*2,MALLOC_CAP_8BIT);
 stripe=heap_caps_malloc(W*16*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);done=xSemaphoreCreateBinary();if(!frame||!stripe||!done)return ESP_ERR_NO_MEM;
 TRY(gpio_set_direction(1,GPIO_MODE_OUTPUT));TRY(gpio_set_level(1,0));
 spi_bus_config_t bus={.sclk_io_num=39,.mosi_io_num=38,.miso_io_num=40,.quadwp_io_num=-1,.quadhd_io_num=-1,.max_transfer_sz=W*16*2};TRY(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO));
 esp_lcd_panel_io_handle_t io;esp_lcd_panel_io_spi_config_t cfg={.dc_gpio_num=42,.cs_gpio_num=45,.pclk_hz=40000000,.lcd_cmd_bits=8,.lcd_param_bits=8,.spi_mode=0,.trans_queue_depth=1,.on_color_trans_done=finished};
 TRY(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,&cfg,&io));
 esp_lcd_panel_dev_config_t pc={.reset_gpio_num=-1,.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB,.bits_per_pixel=16};TRY(esp_lcd_new_panel_st7789(io,&pc,&panel));TRY(esp_lcd_panel_reset(panel));TRY(esp_lcd_panel_init(panel));TRY(esp_lcd_panel_mirror(panel,false,false));TRY(esp_lcd_panel_swap_xy(panel,false));TRY(esp_lcd_panel_invert_color(panel,true));TRY(esp_lcd_panel_disp_on_off(panel,true));lcd_ok=true;
 i2c_config_t ic={.mode=I2C_MODE_MASTER,.sda_io_num=48,.scl_io_num=47,.sda_pullup_en=GPIO_PULLUP_ENABLE,.scl_pullup_en=GPIO_PULLUP_ENABLE,.master.clk_speed=400000};
 esp_err_t e=i2c_param_config(I2C_NUM_0,&ic);if(e==ESP_OK)e=i2c_driver_install(I2C_NUM_0,ic.mode,0,0,0);
 if(e==ESP_OK){uint8_t reg=0xa7,id=0;e=i2c_master_write_read_device(I2C_NUM_0,0x15,&reg,1,&id,1,pdMS_TO_TICKS(100));tp_ok=e==ESP_OK&&id==0xb6;ESP_LOGI("TOUCH","ID %02x, %s",id,esp_err_to_name(e));}
 if(xTaskCreate(ui_task,"ui",4096,NULL,3,NULL)!=pdPASS){lcd_ok=false;return ESP_ERR_NO_MEM;}return ESP_OK;
}
