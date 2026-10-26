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
#include "esp_timer.h"
#include "app.h"
#define W 240
#define H 320
#define PREVIEW_W 320
#define PREVIEW_H 240
#define DMA_ROWS 20
#define TOUCH_POLL_MS 10
#define TOUCH_CONFIRM_SAMPLES 3
#define TOUCH_RELEASE_SAMPLES 3
#define SWIPE_MIN_X 36
#define TAP_MAX_MOVE 18
#define TAP_MIN_MS 30
#define TAP_MAX_MS 500
#define SWIPE_DEBOUNCE_MS 220
#define TAP_DEBOUNCE_MS 120
static uint16_t *frame,*stripe[2];
static esp_lcd_panel_handle_t panel;
static SemaphoreHandle_t done;
static bool lcd_ok=false,tp_ok=false;
static int glitch_count;
static bool filtered_touch=false;
static uint8_t raw_press_streak=0,raw_release_streak=0;
static int filtered_x=0,filtered_y=0;
uint32_t ui_preview_display_ms=0,ui_preview_loop_ms=0,touch_taps=0,touch_swipes=0,touch_rejected=0;
uint16_t ui_preview_fps=0,touch_poll_ms=TOUCH_POLL_MS,touch_swipe_threshold_px=SWIPE_MIN_X,touch_debounce_ms=SWIPE_DEBOUNCE_MS,touch_confirm_samples=TOUCH_CONFIRM_SAMPLES;
bool display_ready(void){return lcd_ok;}
bool touch_ready(void){return tp_ok;}
static inline int iabs_i(int v){return v<0?-v:v;}
static inline uint16_t rgb(uint8_t r,uint8_t g,uint8_t b){uint16_t c=((r&248)<<8)|((g&252)<<3)|(b>>3);return (c>>8)|(c<<8);}
static void rect(int x,int y,int w,int h,uint16_t c){for(int yy=y;yy<y+h&&yy<H;yy++)for(int xx=x;xx<x+w&&xx<W;xx++)if(xx>=0&&yy>=0)frame[yy*W+xx]=c;}
#include "font.inc"
static void tc(int y,const char*s,int z,uint16_t c){text((W-((int)strlen(s)*6*z-z))/2,y,s,z,c);}
static bool finished(esp_lcd_panel_io_handle_t io,esp_lcd_panel_io_event_data_t*ev,void*ctx){BaseType_t wake=pdFALSE;xSemaphoreGiveFromISR(done,&wake);return wake==pdTRUE;}
static bool wait_done(const char *what){if(xSemaphoreTake(done,pdMS_TO_TICKS(1000))==pdTRUE)return true;lcd_ok=false;ESP_LOGE("LCD","%s DMA timeout",what);return false;}
static void blit(int width,int height,const char *what){
 int pending=0,seq=0;
 for(int y=0;y<height;y+=DMA_ROWS){int rows=(y+DMA_ROWS<=height)?DMA_ROWS:(height-y);if(pending==2){if(!wait_done(what)){vTaskDelete(NULL);return;}pending--;}
  int slot=seq&1;memcpy(stripe[slot],frame+(size_t)y*width,(size_t)width*rows*2);
  if(esp_lcd_panel_draw_bitmap(panel,0,y,width,y+rows,stripe[slot])!=ESP_OK){lcd_ok=false;ESP_LOGE("LCD","%s draw failed",what);while(pending>0){wait_done(what);pending--;}return;}pending++;seq++;}
 while(pending>0){if(!wait_done(what)){vTaskDelete(NULL);return;}pending--;}
}
static void show(void){blit(W,H,"UI");}
static void show_preview(void){blit(PREVIEW_W,PREVIEW_H,"Preview");}
static esp_err_t preview_orientation(bool enabled){esp_err_t e;if(enabled){e=esp_lcd_panel_swap_xy(panel,true);if(e!=ESP_OK)return e;return esp_lcd_panel_mirror(panel,true,false);}e=esp_lcd_panel_mirror(panel,false,false);if(e!=ESP_OK)return e;return esp_lcd_panel_swap_xy(panel,false);}
static esp_err_t render_preview(void){int64_t start=esp_timer_get_time();esp_err_t e=camera_preview_frame((uint8_t*)frame,W*H*2);if(e!=ESP_OK)return e;int64_t output_start=esp_timer_get_time();for(size_t i=0;i<(size_t)W*H;i++){uint16_t p=frame[i];frame[i]=(p>>8)|(p<<8);}show_preview();int64_t end=esp_timer_get_time();ui_preview_display_ms=(uint32_t)((end-output_start+999)/1000);ui_preview_loop_ms=(uint32_t)((end-start+999)/1000);ui_preview_fps=ui_preview_loop_ms?(uint16_t)(1000U/ui_preview_loop_ms):0;return lcd_ok?ESP_OK:ESP_FAIL;}
static void draw(int page,int phase){
 uint16_t wh=rgb(240,245,235),accent=rgb(255,191,69),dim=rgb(77,88,94),ok=rgb(137,239,194);rect(0,0,W,H,rgb(7,13,18));
 if(page==0){tc(58,"PRISMA P",4,wh);uint16_t pal[]={rgb(255,191,69),rgb(137,239,194),rgb(250,246,220),rgb(196,142,84),rgb(100,185,168)};tc(110,"CAMERA",3,pal[phase%5]);rect(42,155,156,2,accent);tc(184,"TAP FOR GLITCH",1,wh);tc(214,"SWIPE TO CONTINUE",1,dim);tc(255,"BUILD 006",1,accent);
 }else if(page==1){char sensor[40],pid[20],src[32],pv[32];tc(16,"PRISMA P",3,wh);tc(47,"CAMERA + VGA STREAM",1,accent);tc(77,camera_ready?"CAMERA READY":camera_probed?"CAMERA ERROR":"CAMERA PROBING",2,camera_ready?ok:accent);snprintf(sensor,sizeof(sensor),"SENSOR %s",camera_sensor_name);tc(114,sensor,1,camera_ready?wh:dim);snprintf(pid,sizeof(pid),"PID %04X",camera_pid);tc(135,pid,1,camera_ready?wh:dim);tc(160,camera_capture_ready?"LOCAL PREVIEW READY":camera_capture_attempted?"PREVIEW UNAVAILABLE":"CAPTURE PENDING",1,camera_capture_ready?ok:accent);if(camera_capture_ready){snprintf(src,sizeof(src),"SOURCE %ux%u %s",camera_frame_width,camera_frame_height,camera_frame_format);tc(187,src,1,wh);if(camera_preview_frames){snprintf(pv,sizeof(pv),"LCD %u FPS / %u MS",(unsigned)ui_preview_fps,(unsigned)ui_preview_loop_ms);tc(211,pv,1,wh);}else tc(211,"LCD OUTPUT 320x240",1,dim);tc(246,"TAP TO PREVIEW",2,accent);tc(273,"FRESH TAP TO EXIT",1,dim);}else tc(220,"STREAM PENDING",1,dim);
 }else{char cam[40];tc(17,"ADMINISTRATION",2,accent);tc(59,network_ready?"WIFI AP ACTIVE":"NETWORK ERROR",1,wh);tc(87,app_ssid,1,wh);tc(116,"WIFI PASSWORD",1,dim);tc(138,app_password,2,accent);tc(184,network_ready?"192.168.4.1":"CHECK USB LOG",2,wh);snprintf(cam,sizeof(cam),"CAMERA %s",camera_ready?camera_sensor_name:camera_probed?"ERROR":"PROBING");tc(224,cam,1,camera_ready?ok:dim);tc(246,stream_server_ready?"WEB STREAM :81 READY":"WEB STREAM ERROR",1,stream_server_ready?ok:accent);tc(268,"ADMIN PORT 80",1,dim);}
 for(int i=0;i<3;i++)rect(96+i*20,297,8,8,i==page?accent:dim);show();
}
static void glitch(int x,int y){uint16_t pal[]={rgb(255,191,69),rgb(137,239,194),rgb(250,246,220),rgb(196,142,84),rgb(100,185,168)};for(int i=0;i<9;i++){int dx=((i*17+glitch_count*11)%55)-27,dy=((i*23+glitch_count*7)%55)-27,sz=4+((i+glitch_count)%4)*3;rect(x+dx,y+dy,sz,sz,pal[(i+glitch_count)%5]);}glitch_count++;show();}
static esp_err_t read_touch_raw(bool *pressed,int*x,int*y){uint8_t reg=2,b[5];esp_err_t e=i2c_master_write_read_device(I2C_NUM_0,0x15,&reg,1,b,5,pdMS_TO_TICKS(30));if(e!=ESP_OK)return e;uint8_t fingers=b[0]&15;*pressed=(fingers==1);*x=((b[1]&15)<<8)|b[2];*y=((b[3]&15)<<8)|b[4];if(*x>=W||*y>=H)*pressed=false;return ESP_OK;}
static esp_err_t read_touch(bool *pressed,int*x,int*y){bool raw=false;int rx=0,ry=0;esp_err_t e=read_touch_raw(&raw,&rx,&ry);if(e!=ESP_OK)return e;if(raw){raw_release_streak=0;filtered_x=rx;filtered_y=ry;if(!filtered_touch){if(raw_press_streak<255)raw_press_streak++;if(raw_press_streak>=TOUCH_CONFIRM_SAMPLES){filtered_touch=true;raw_press_streak=0;}}}else{if(!filtered_touch&&raw_press_streak>0){touch_rejected++;raw_press_streak=0;}if(filtered_touch){if(raw_release_streak<255)raw_release_streak++;if(raw_release_streak>=TOUCH_RELEASE_SAMPLES){filtered_touch=false;raw_release_streak=0;}}else raw_release_streak=0;}*pressed=filtered_touch;*x=filtered_x;*y=filtered_y;return ESP_OK;}
static void exit_preview(int page,int phase,bool *preview,int64_t *block_until){camera_preview_set_active(false);*preview=false;esp_err_t oe=preview_orientation(false);if(oe!=ESP_OK)ESP_LOGE("LCD","Restore orientation: %s",esp_err_to_name(oe));draw(page,phase);*block_until=esp_timer_get_time()+(int64_t)SWIPE_DEBOUNCE_MS*1000;}
static void ui_task(void*unused){
 int page=0,phase=0,anim=0,sx=0,sy=0,lx=0,ly=0,release_misses=0;bool down=false,wait_release=false,preview=false,preview_armed=false;int64_t touch_start=0,block_until=0;draw(page,phase);gpio_set_level(1,1);
 while(1){bool touched=false;int x=0,y=0;esp_err_t e=tp_ok?read_touch(&touched,&x,&y):ESP_FAIL;int64_t now=esp_timer_get_time();
  if(preview){
   if(!preview_armed){if(e==ESP_OK&&!touched&&now>=block_until){preview_armed=true;down=false;release_misses=0;}esp_err_t pe=render_preview();if(pe!=ESP_OK){ESP_LOGE("LCD","Preview render: %s",esp_err_to_name(pe));exit_preview(page,phase,&preview,&block_until);}vTaskDelay(pdMS_TO_TICKS(1));continue;}
   if(e==ESP_OK&&now>=block_until){if(touched){release_misses=0;if(!down){down=true;sx=lx=x;sy=ly=y;touch_start=now;}else{lx=x;ly=y;}}else if(down){if(++release_misses>=1){int adx=iabs_i(lx-sx),ady=iabs_i(ly-sy);int64_t held=(now-touch_start)/1000;down=false;release_misses=0;if(adx<=24&&ady<=24&&held>=TAP_MIN_MS&&held<=700){touch_taps++;exit_preview(page,phase,&preview,&block_until);preview_armed=false;vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_MS));continue;}}}}
   esp_err_t pe=render_preview();if(pe!=ESP_OK){ESP_LOGE("LCD","Preview render: %s",esp_err_to_name(pe));exit_preview(page,phase,&preview,&block_until);preview_armed=false;down=false;release_misses=0;}vTaskDelay(pdMS_TO_TICKS(1));continue;
  }
  if(e==ESP_OK){if(wait_release){if(!touched){if(++release_misses>=2){wait_release=false;release_misses=0;}}else release_misses=0;}else if(now>=block_until){if(touched){release_misses=0;if(!down){down=true;sx=lx=x;sy=ly=y;touch_start=now;}else{lx=x;ly=y;int dx=x-sx,dy=y-sy,adx=iabs_i(dx),ady=iabs_i(dy);if(adx>=SWIPE_MIN_X&&adx>=ady+10&&((dx<0&&page<2)||(dx>0&&page>0))){page+=dx<0?1:-1;touch_swipes++;glitch_count=0;draw(page,phase);down=false;wait_release=true;release_misses=0;block_until=now+(int64_t)SWIPE_DEBOUNCE_MS*1000;}}}else if(down){if(++release_misses>=2){int adx=iabs_i(lx-sx),ady=iabs_i(ly-sy);int64_t held=(now-touch_start)/1000;down=false;release_misses=0;if(adx<=TAP_MAX_MOVE&&ady<=TAP_MAX_MOVE&&held>=TAP_MIN_MS&&held<=TAP_MAX_MS){if(page==0){touch_taps++;glitch(sx,sy);block_until=now+(int64_t)TAP_DEBOUNCE_MS*1000;}else if(page==1&&camera_capture_ready){esp_err_t oe=preview_orientation(true);if(oe==ESP_OK){touch_taps++;camera_preview_set_active(true);preview=true;preview_armed=false;block_until=now+(int64_t)TAP_DEBOUNCE_MS*1000;}else ESP_LOGE("LCD","Preview orientation: %s",esp_err_to_name(oe));}}}}}}
  if(page==0&&glitch_count==0&&++anim>=12){anim=0;phase=(phase+1)%5;draw(page,phase);}vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_MS));
 }
}
#define TRY(expr) do{esp_err_t e=(expr);if(e!=ESP_OK)return e;}while(0)
esp_err_t display_start(void){
 frame=heap_caps_malloc(W*H*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!frame)frame=heap_caps_malloc(W*H*2,MALLOC_CAP_8BIT);stripe[0]=heap_caps_malloc(PREVIEW_W*DMA_ROWS*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);stripe[1]=heap_caps_malloc(PREVIEW_W*DMA_ROWS*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);done=xSemaphoreCreateCounting(2,0);if(!frame||!stripe[0]||!stripe[1]||!done)return ESP_ERR_NO_MEM;
 TRY(gpio_set_direction(1,GPIO_MODE_OUTPUT));TRY(gpio_set_level(1,0));spi_bus_config_t bus={.sclk_io_num=39,.mosi_io_num=38,.miso_io_num=40,.quadwp_io_num=-1,.quadhd_io_num=-1,.max_transfer_sz=PREVIEW_W*DMA_ROWS*2};TRY(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO));
 esp_lcd_panel_io_handle_t io;esp_lcd_panel_io_spi_config_t cfg={.dc_gpio_num=42,.cs_gpio_num=45,.pclk_hz=40000000,.lcd_cmd_bits=8,.lcd_param_bits=8,.spi_mode=0,.trans_queue_depth=2,.on_color_trans_done=finished};TRY(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,&cfg,&io));esp_lcd_panel_dev_config_t pc={.reset_gpio_num=-1,.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB,.bits_per_pixel=16};TRY(esp_lcd_new_panel_st7789(io,&pc,&panel));TRY(esp_lcd_panel_reset(panel));TRY(esp_lcd_panel_init(panel));TRY(esp_lcd_panel_mirror(panel,false,false));TRY(esp_lcd_panel_swap_xy(panel,false));TRY(esp_lcd_panel_invert_color(panel,true));TRY(esp_lcd_panel_disp_on_off(panel,true));lcd_ok=true;
 i2c_config_t ic={.mode=I2C_MODE_MASTER,.sda_io_num=48,.scl_io_num=47,.sda_pullup_en=GPIO_PULLUP_ENABLE,.scl_pullup_en=GPIO_PULLUP_ENABLE,.master.clk_speed=400000};esp_err_t e=i2c_param_config(I2C_NUM_0,&ic);if(e==ESP_OK)e=i2c_driver_install(I2C_NUM_0,ic.mode,0,0,0);if(e==ESP_OK){uint8_t reg=0xa7,id=0;e=i2c_master_write_read_device(I2C_NUM_0,0x15,&reg,1,&id,1,pdMS_TO_TICKS(100));tp_ok=e==ESP_OK&&id==0xb6;ESP_LOGI("TOUCH","ID %02x %s; confirm=%u poll=%u swipe=%u debounce=%u",id,esp_err_to_name(e),touch_confirm_samples,touch_poll_ms,touch_swipe_threshold_px,touch_debounce_ms);}
 if(xTaskCreate(ui_task,"ui",7168,NULL,3,NULL)!=pdPASS){lcd_ok=false;return ESP_ERR_NO_MEM;}return ESP_OK;
}
