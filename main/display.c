#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "app.h"
#include "ui_model.h"
#include "esp_timer.h"
#include "esp_system.h"

#define LCD_W 240
#define LCD_H 320
#define DMA_ROWS 20
#define MARKER_Y (LCD_H - 16)
#define TOUCH_POLL_MS 25

static const char *TAG = "CS012B_LCD";
static uint16_t *frame;
static uint16_t *stripe[2];
static esp_lcd_panel_handle_t panel;
static SemaphoreHandle_t dma_done;
static bool s_ready;

bool display_ready(void) { return s_ready; }

static inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = (uint16_t)(((r & 248U) << 8) | ((g & 252U) << 3) | (b >> 3));
    return (uint16_t)((c >> 8) | (c << 8));
}

static void rect(int x, int y, int w, int h, uint16_t c)
{
    for (int yy = y; yy < y + h && yy < LCD_H; ++yy) {
        if (yy < 0) continue;
        for (int xx = x; xx < x + w && xx < LCD_W; ++xx) {
            if (xx >= 0) frame[(size_t)yy * LCD_W + xx] = c;
        }
    }
}

#include "font.inc"

static void centered_text(int y, const char *s, int scale, uint16_t color)
{
    int width = (int)strlen(s) * 6 * scale - scale;
    text((LCD_W - width) / 2, y, s, scale, color);
}

static bool color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *ctx)
{
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(dma_done, &wake);
    return wake == pdTRUE;
}

static esp_err_t wait_dma(void)
{
    return xSemaphoreTake(dma_done, pdMS_TO_TICKS(1000)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static esp_err_t present_region(int y0, int y1)
{
    int pending = 0;
    int seq = 0;
    for (int y = y0; y < y1; y += DMA_ROWS) {
        int rows = (y + DMA_ROWS <= y1) ? DMA_ROWS : (y1 - y);
        if (pending == 2) {
            esp_err_t e = wait_dma();
            if (e != ESP_OK) return e;
            --pending;
        }
        int slot = seq & 1;
        memcpy(stripe[slot], frame + (size_t)y * LCD_W, (size_t)LCD_W * rows * 2U);
        esp_err_t e = esp_lcd_panel_draw_bitmap(panel, 0, y, LCD_W, y + rows, stripe[slot]);
        if (e != ESP_OK) return e;
        ++pending;
        ++seq;
    }
    while (pending > 0) {
        esp_err_t e = wait_dma();
        if (e != ESP_OK) return e;
        --pending;
    }
    return ESP_OK;
}

static esp_err_t present_once(void)
{
    return present_region(0, LCD_H);
}

/* Original PRISMA-PI black palette, two LCD pages only:
 * CONFIG(0) at left and HOME(1) at right. Web Admin retains all modules.
 */
#define REFERENCE_SCENE_Y 40
#define REFERENCE_SCENE_H 240

static uint16_t moving_color(unsigned tick, unsigned offset)
{
    static const uint8_t palette[][3] = {
        {30,120,255}, {175,55,255}, {255,35,150},
        {255,120,25}, {45,225,120}
    };
    unsigned i = ((tick / 6U) + offset) % 5U;
    return rgb(palette[i][0], palette[i][1], palette[i][2]);
}

/*
 * Exact legacy algorithm from prisma-pi-lite-waveshare/main/main.c:
 * nine colourful rectangles per tap, count-dependent offsets. We do not clear
 * the scene here, so every previous tap remains visible until page navigation.
 */
static void accumulate_glitch(unsigned glitch_index, int x, int y)
{
    for (unsigned i = 0; i < 9U; i++) {
        ui_glitch_rect_t block = ui_model_glitch_rect(glitch_index,i,x,y);
        if (block.x < 0 || block.x + block.size > LCD_W ||
            block.y < REFERENCE_SCENE_Y ||
            block.y + block.size > REFERENCE_SCENE_Y + REFERENCE_SCENE_H) continue;
        rect(block.x, block.y, block.size, block.size,
             moving_color(0, block.palette_index));
    }
}

static void render_home(unsigned phase)
{
    const uint16_t bg = rgb(4,7,12);
    const uint16_t white = rgb(240,245,250);
    rect(0,0,LCD_W,LCD_H,bg);

    /* Original logo palette: -PI index (glyph + phase) % 5. */
    text(14,REFERENCE_SCENE_Y+39,"PRISMA",4,white);
    text(158,REFERENCE_SCENE_Y+39,"-",4,moving_color(phase * 6U,0));
    text(182,REFERENCE_SCENE_Y+39,"P",4,moving_color(phase * 6U,1));
    text(206,REFERENCE_SCENE_Y+39,"I",4,moving_color(phase * 6U,2));
    centered_text(REFERENCE_SCENE_Y+118,"CAMERA",3,white);
    centered_text(REFERENCE_SCENE_Y+156,"SYNTH",3,white);
    /* Only HOME and PHOTO are operational pages; SYSTEM is hidden. */
    rect(104,304,6,6,rgb(35,220,220));
    rect(118,304,6,6,rgb(45,60,75));
    rect(132,304,6,6,rgb(45,60,75));
}


/* Exactly the original PRISMA SYSTEM navigation: a hidden lateral gate,
 * two actionable buttons, then five internal settings screens.
 * This is independent of the PRISMA HOME page indicators.
 */
#define SETTINGS_COUNT 5
#define SETTINGS_DOTS_Y 307
typedef enum { LCD_NAV_PRISMA=0, LCD_NAV_GATE, LCD_NAV_SETTINGS, LCD_NAV_CONFIRM, LCD_NAV_PHOTO, LCD_NAV_FILES } lcd_nav_t;
typedef enum { LCD_CONFIRM_NONE=0, LCD_CONFIRM_NETWORK, LCD_CONFIRM_ADMIN } lcd_confirm_t;

static void centered_button(int y, int h, const char *label)
{
    const uint16_t fill = rgb(30,48,60);
    const uint16_t white = rgb(240,245,250);
    rect(25,y,190,h,fill);
    centered_text(y + (h - 14) / 2,label,2,white);
}

static void settings_dots(int active)
{
    const uint16_t cyan=rgb(35,220,220),dim=rgb(45,60,75);
    const int start=(LCD_W - (SETTINGS_COUNT*6+(SETTINGS_COUNT-1)*8))/2;
    for(int i=0;i<SETTINGS_COUNT;i++)
        rect(start+i*14, SETTINGS_DOTS_Y,6,6,i==active?cyan:dim);
}

static char s_photo_message[32]="READY";
static uint32_t s_last_photo_tap_ms;
static void render_photo(void)
{
    const uint16_t wh=rgb(240,245,250),cy=rgb(35,220,220),dim=rgb(75,82,90);
    photo_state_t p={0};photo_state_get(&p);
    static const char * const sizes[]={"VGA","SVGA","XGA","SXGA","UXGA","QXGA","5MP"};
    static const char * const qualities[]={"STANDARD","HIGH","MAX"};
    rect(0,0,LCD_W,LCD_H,rgb(4,7,12));
    centered_text(8,"PHOTO",3,cy);
    centered_text(44,p.enabled?"CAMERA READY":"CAMERA OFF",2,p.enabled?cy:dim);
    char label[40];
    snprintf(label,sizeof(label),"SIZE %s",sizes[p.size_index<7?p.size_index:1]);
    centered_button(73,30,label);
    snprintf(label,sizeof(label),"JPEG %s",qualities[p.quality_index<3?p.quality_index:1]);
    centered_button(109,30,label);
    centered_button(153,32,"CAMERA ON");
    centered_button(193,32,"TAKE PHOTO");
    centered_button(233,32,"CAMERA OFF");
    centered_text(272,s_photo_message,1,cy);
    centered_text(291,"SWIPE LEFT FILES",1,dim);
    rect(104,304,6,6,dim);
    rect(118,304,6,6,cy);
    rect(132,304,6,6,dim);
}

static char s_files_path[FILES_LCD_PATH_MAX+1]="/";
static char s_files_volume[12]="internal";
static files_lcd_page_t s_files_page;
static uint32_t s_files_offset;
static char s_files_status[38]="";
static uint32_t s_files_last_tap;

static void files_refresh(void)
{
    esp_err_t e=files_lcd_list(s_files_volume,s_files_path,s_files_offset,&s_files_page);
    if(e!=ESP_OK){
        memset(&s_files_page,0,sizeof(s_files_page));
        snprintf(s_files_status,sizeof(s_files_status),"FILES ERROR 0x%X",(unsigned)e);
    }else{
        snprintf(s_files_status,sizeof(s_files_status),"%lu FILES",(unsigned long)s_files_page.total);
    }
}

static void files_parent(void)
{
    if(!strcmp(s_files_path,"/"))return;
    char *slash=strrchr(s_files_path,'/');
    if(slash){
        if(slash==s_files_path)s_files_path[1]=0;
        else *slash=0;
    }
    s_files_offset=0;
    files_refresh();
}

static void files_select(int index)
{
    if(index<0||index>=s_files_page.count)return;
    const files_lcd_entry_t *file=&s_files_page.rows[index];
    if(file->directory){
        char new_path[FILES_LCD_PATH_MAX+1];
        int n=snprintf(new_path,sizeof(new_path),"%s%s%s",s_files_path,
                 !strcmp(s_files_path,"/")?"":"/",file->name);
        if(n<=0||n>=(int)sizeof(new_path)){
            strlcpy(s_files_status,"PATH TOO LONG",sizeof(s_files_status));
            return;
        }
        strlcpy(s_files_path,new_path,sizeof(s_files_path));
        s_files_offset=0;
        files_refresh();
    }else{
        snprintf(s_files_status,sizeof(s_files_status),"%.21s %lu KB",
                 file->name,(unsigned long)((file->bytes+1023U)/1024U));
    }
}

static void render_files(void)
{
    const uint16_t wh=rgb(240,245,250),cy=rgb(35,220,220),dim=rgb(75,82,90);
    rect(0,0,LCD_W,LCD_H,rgb(4,7,12));
    centered_text(12,"FILES",3,cy);
    centered_button(52,29,!strcmp(s_files_volume,"internal")?"INTERNAL / SD":"SD / INTERNAL");
    char title[43];
    snprintf(title,sizeof(title),"%.32s",s_files_path);
    text(9,92,title,1,wh);
    for(int i=0;i<FILES_LCD_ROWS;i++){
        int y=112+i*34;
        if(i>=s_files_page.count)continue;
        const files_lcd_entry_t *file=&s_files_page.rows[i];
        rect(8,y,224,30,rgb(22,40,51));
        char name[37];
        snprintf(name,sizeof(name),"%s%.27s",file->directory?"D ":"F ",file->name);
        text(14,y+10,name,1,file->directory?cy:wh);
    }
    rect(8,252,71,32,rgb(30,48,60));
    rect(84,252,72,32,rgb(30,48,60));
    rect(161,252,71,32,rgb(30,48,60));
    text(19,261,"UP",2,wh);
    text(92,261,"PREV",2,wh);
    text(167,261,"NEXT",2,wh);
    text(6,290,s_files_status,1,dim);
    rect(104,304,6,6,dim);
    rect(118,304,6,6,dim);
    rect(132,304,6,6,cy);
}

static void render_gate(void)
{
    const uint16_t wh=rgb(240,245,250),cy=rgb(35,220,220);
    rect(0,0,LCD_W,LCD_H,rgb(4,7,12));
    centered_text(62,"PRISMA SYSTEM",2,cy);
    centered_text(97,"CAMERA SYNTH",2,wh);
    centered_button(132,48,"CONFIG");
    centered_button(210,48,"BACK TO PRISMA");
    /* No dots: SYSTEM GATE is deliberately hidden from PRISMA's carousel. */
}

static void settings_footer(int page)
{
    const uint16_t dim=rgb(75,82,90);
    static const char * const numbers[SETTINGS_COUNT] = {
        "1 OF 5", "2 OF 5", "3 OF 5", "4 OF 5", "5 OF 5"
    };
    centered_text(246,numbers[page>=0&&page<SETTINGS_COUNT?page:0],1,dim);
    centered_button(266,30,"BACK TO PRISMA");
    settings_dots(page);
}

static void render_settings(int page)
{
    const uint16_t wh=rgb(240,245,250),cy=rgb(35,220,220),dim=rgb(75,82,90);
    char b[48]={0};
    rect(0,0,LCD_W,LCD_H,rgb(4,7,12));
    if(page==0){
        prisma_settings_t cfg={0};settings_get(&cfg);
        network_state_t net={0};network_get_state(&net);
        discovery_state_t discovery={0};discovery_get_state(&discovery);
        centered_text(17,"DEVICE",3,cy);
        text(12,52,"NAME",1,dim);
        text(12,66,cfg.friendly_name[0]?cfg.friendly_name:"NOT SET",1,wh);
        text(12,86,"UNIQUE HOSTNAME",1,dim);
        /* Hostname is DNS-lowercase, but the 5x7 LCD font has ONLY
         * uppercase glyphs. Convert a DISPLAY COPY, never the DNS config. */
        const char *raw_host=discovery.hostname[0]?discovery.hostname:cfg.hostname;
        char host_lcd[40]={0};
        strlcpy(host_lcd,raw_host,sizeof(host_lcd));
        for(size_t i=0;host_lcd[i];i++){
            if(host_lcd[i]>='a'&&host_lcd[i]<='z')
                host_lcd[i]=(char)(host_lcd[i]-'a'+'A');
        }
        text(12,100,host_lcd,1,cy);
        text(12,120,"MDNS ADDRESS",1,dim);
        char mdns_lcd[48]={0};
        snprintf(mdns_lcd,sizeof(mdns_lcd),"%s.LOCAL",host_lcd);
        /* Up to 38 glyphs fit within 240px at 6px per glyph. */
        text(6,134,mdns_lcd,1,wh);
        text(12,154,"DIRECT AP ADDRESS",1,dim);
        text(12,169,net.ip[0]?net.ip:"192.168.4.1",2,cy);
        text(12,197,"LAN IP",1,dim);
        text(12,211,net.sta_connected&&net.sta_ip[0]?net.sta_ip:"NOT CONNECTED",1,wh);
        centered_text(235,"ADMIN VIA BROWSER",1,cy);
    }else if(page==1){
        network_state_t net={0};network_get_state(&net);
        centered_text(24,"NETWORK",3,cy);
        centered_text(65,"WIFI STATION",1,dim);
        centered_text(84,net.sta_configured?net.sta_ssid:"NOT CONFIGURED",1,wh);
        centered_text(108,net.sta_connected?"CONNECTED":"DISCONNECTED",1,net.sta_connected?cy:wh);
        centered_text(131,net.sta_connected?net.sta_ip:"NO STA IP",1,wh);
        centered_text(157,"ACCESS POINT",1,dim);
        centered_text(174,net.ssid,1,wh);
        centered_button(197,35,"RESET NETWORK");
    }else if(page==2){
        storage_state_t st={0};storage_get_state(&st);
        centered_text(24,"STORAGE",3,cy);
        centered_text(74,st.internal.mounted?"INTERNAL READY":"INTERNAL OFF",2,wh);
        snprintf(b,sizeof(b),"USED %u KB",(unsigned)(st.internal.used_bytes/1024U));
        centered_text(111,b,2,wh);
        snprintf(b,sizeof(b),"FREE %u KB",(unsigned)(st.internal.free_bytes/1024U));
        centered_text(146,b,2,wh);
        centered_text(189,st.sd.mounted?"MICROSD READY":"MICROSD OFF",2,st.sd.mounted?cy:dim);
    }else if(page==3){
        centered_text(24,"SECURITY",3,cy);
        centered_text(78,"ADMIN USER",1,dim);
        centered_text(100,"admin",2,wh);
        centered_text(132,"PASSWORD",1,dim);
        centered_text(153,auth_is_enabled()?"CONFIGURED":"NOT CONFIGURED",2,wh);
        centered_button(197,35,"RESET ADMIN PASS");
    }else{
        centered_text(24,"SYSTEM",3,cy);
        centered_text(70,"CS012B PLATFORM",2,wh);
        snprintf(b,sizeof(b),"UP %llu SEC",(unsigned long long)(esp_timer_get_time()/1000000ULL));
        centered_text(115,b,2,wh);
        snprintf(b,sizeof(b),"HEAP %u KB",(unsigned)(esp_get_free_heap_size()/1024U));
        centered_text(151,b,2,wh);
        centered_text(190,"ADMIN SERVICES",2,cy);
    }
    settings_footer(page);
}

static void render_confirm(lcd_confirm_t confirm, const char *result)
{
    const uint16_t wh=rgb(240,245,250),cy=rgb(35,220,220),dim=rgb(75,82,90);
    rect(0,0,LCD_W,LCD_H,rgb(4,7,12));
    centered_text(53,confirm==LCD_CONFIRM_NETWORK?"RESET NETWORK":"RESET ADMIN PASS",2,cy);
    centered_text(100,"ARE YOU SURE",2,wh);
    centered_text(132,confirm==LCD_CONFIRM_NETWORK?"FORGET SAVED WIFI":"REMOVE ADMIN PASS",1,wh);
    centered_text(148,confirm==LCD_CONFIRM_NETWORK?"KEEP DEVICE DATA":"CLEAR CREDENTIALS",1,dim);
    if(result&&result[0])centered_text(177,result,1,cy);
    rect(15,210,100,40,rgb(30,48,60));text(25,222,"CANCEL",2,wh);
    rect(125,210,100,40,rgb(30,48,60));text(145,222,"RESET",2,wh);
    centered_text(276,"CONFIRM REQUIRED",1,dim);
}

static void display_task(void *unused)
{
    ui_model_t ui;
    ui_model_init(&ui,(uint32_t)(esp_timer_get_time()/1000ULL));
    lcd_nav_t nav=LCD_NAV_PRISMA;
    lcd_confirm_t confirm=LCD_CONFIRM_NONE;
    int settings_page=0;
    uint32_t last_redraw=0;
    char last_result[32]={0};
    bool initial=true;
    for(;;){
        uint32_t now=(uint32_t)(esp_timer_get_time()/1000ULL);
        int action=touch_hw_poll_action();
        uint16_t tx=120,ty=150;
        if(action)touch_hw_last_point(&tx,&ty);
        ui_change_t change=UI_CHANGE_NONE;
        bool redraw=initial;
        initial=false;

        if(nav==LCD_NAV_PRISMA){
            if(action==UI_EVENT_SWIPE_RIGHT&&ui.page==UI_PAGE_HOME){
                ui_model_input(&ui,action,now);
                nav=LCD_NAV_GATE;
                redraw=true;
            }else if(action==UI_EVENT_SWIPE_LEFT){
                nav=LCD_NAV_PHOTO;
                strlcpy(s_photo_message,"READY",sizeof(s_photo_message));
                redraw=true;
            }else if(action==UI_EVENT_TAP){
                change=ui_model_input(&ui,action,now);
            }
        }else if(nav==LCD_NAV_PHOTO){
            if(action==UI_EVENT_SWIPE_LEFT){
                nav=LCD_NAV_FILES;
                files_refresh();
                redraw=true;
            }else if(action==UI_EVENT_SWIPE_RIGHT){
                ui_model_init(&ui,now);
                ui.home_cooldown_until_ms=now+500U;
                nav=LCD_NAV_PRISMA;
                redraw=true;
            }else if(action==UI_EVENT_TAP && (uint32_t)(now-s_last_photo_tap_ms)>=850U){
                s_last_photo_tap_ms=now;
                esp_err_t result=ESP_ERR_INVALID_ARG;
                photo_state_t cfg={0};photo_state_get(&cfg);
                if(ty>=70&&ty<108){
                    result=photo_set_options((uint8_t)((cfg.size_index+1U)%7U),cfg.quality_index);
                    strlcpy(s_photo_message,result==ESP_OK?"SIZE APPLIED":"SIZE FAILED",sizeof(s_photo_message));
                    redraw=true;
                }else if(ty>=108&&ty<146){
                    result=photo_set_options(cfg.size_index,(uint8_t)((cfg.quality_index+1U)%3U));
                    strlcpy(s_photo_message,result==ESP_OK?"JPEG APPLIED":"JPEG FAILED",sizeof(s_photo_message));
                    redraw=true;
                }else if(ty>=148&&ty<189){
                    result=photo_power(true);
                    strlcpy(s_photo_message,result==ESP_OK?"CAMERA READY":"ENABLE FAILED",sizeof(s_photo_message));
                    redraw=true;
                }else if(ty>=189&&ty<230){
                    char saved[48]={0};
                    result=photo_take(NULL,saved,sizeof(saved));
                    if(result==ESP_OK)
                        snprintf(s_photo_message,sizeof(s_photo_message),"SAVED %.22s",saved+8);
                    else
                        snprintf(s_photo_message,sizeof(s_photo_message),"ERROR 0x%X",(unsigned)result);
                    redraw=true;
                }else if(ty>=230&&ty<268){
                    result=photo_power(false);
                    strlcpy(s_photo_message,result==ESP_OK?"CAMERA OFF":"DISABLE FAILED",sizeof(s_photo_message));
                    redraw=true;
                }
            }
        }else if(nav==LCD_NAV_FILES){
            if(action==UI_EVENT_SWIPE_RIGHT){
                nav=LCD_NAV_PHOTO;
                redraw=true;
            }else if(action==UI_EVENT_TAP&&(uint32_t)(now-s_files_last_tap)>350U){
                s_files_last_tap=now;
                if(ty>=50&&ty<87){
                    strlcpy(s_files_volume,!strcmp(s_files_volume,"internal")?"sd":"internal",sizeof(s_files_volume));
                    strlcpy(s_files_path,"/",sizeof(s_files_path));
                    s_files_offset=0;files_refresh();redraw=true;
                }else if(ty>=110&&ty<250){
                    int index=((int)ty-112)/34;
                    if(index>=0&&index<FILES_LCD_ROWS){files_select(index);redraw=true;}
                }else if(ty>=252&&ty<289){
                    if(tx<81)files_parent();
                    else if(tx<158){
                        s_files_offset=s_files_offset>=FILES_LCD_ROWS?s_files_offset-FILES_LCD_ROWS:0;
                        files_refresh();
                    }else{
                        if(s_files_offset+FILES_LCD_ROWS<s_files_page.total)
                            s_files_offset+=FILES_LCD_ROWS;
                        files_refresh();
                    }
                    redraw=true;
                }
            }
        }else if(nav==LCD_NAV_GATE){
            if(action==UI_EVENT_SWIPE_LEFT){
                ui_model_init(&ui,now);
                ui.home_cooldown_until_ms=now+500U;
                nav=LCD_NAV_PRISMA;
                redraw=true;
            }else if(action==UI_EVENT_TAP){
                if(ty>=124&&ty<190){nav=LCD_NAV_SETTINGS;settings_page=0;redraw=true;}
                else if(ty>=202&&ty<268){
                    ui_model_init(&ui,now);
                    ui.home_cooldown_until_ms=now+500U;
                    nav=LCD_NAV_PRISMA;redraw=true;
                }
            }
        }else if(nav==LCD_NAV_SETTINGS){
            if(action==UI_EVENT_SWIPE_LEFT){
                if(settings_page<SETTINGS_COUNT-1){settings_page++;redraw=true;}
            }else if(action==UI_EVENT_SWIPE_RIGHT){
                if(settings_page>0){settings_page--;redraw=true;}
            }else if(action==UI_EVENT_TAP){
                if(ty>=260&&ty<=305){
                    ui_model_init(&ui,now);
                    ui.home_cooldown_until_ms=now+500U;
                    nav=LCD_NAV_PRISMA;redraw=true;
                }else if(ty>=190&&ty<240&&settings_page==1){
                    confirm=LCD_CONFIRM_NETWORK;nav=LCD_NAV_CONFIRM;last_result[0]=0;redraw=true;
                }else if(ty>=190&&ty<240&&settings_page==3){
                    confirm=LCD_CONFIRM_ADMIN;nav=LCD_NAV_CONFIRM;last_result[0]=0;redraw=true;
                }
            }
        }else if(nav==LCD_NAV_CONFIRM&&action==UI_EVENT_TAP){
            if(ty>=200&&ty<=265){
                if(tx<120){nav=LCD_NAV_SETTINGS;confirm=LCD_CONFIRM_NONE;redraw=true;}
                else{
                    /* Physical confirmation required; neither reset touches factory data. */
                    esp_err_t e=confirm==LCD_CONFIRM_NETWORK?
                        network_forget_sta():auth_factory_reset();
                    ESP_LOGW(TAG,"local confirmed reset %s => %s",
                             confirm==LCD_CONFIRM_NETWORK?"network":"admin",esp_err_to_name(e));
                    platform_log_event(e==ESP_OK?"WARN":"ERROR","local_admin",
                                       confirm==LCD_CONFIRM_NETWORK?
                                       "Confirmed forget saved STA Wi-Fi":"Confirmed admin password reset");
                    snprintf(last_result,sizeof(last_result),"%s",e==ESP_OK?"RESET DONE":"RESET FAILED");
                    confirm=LCD_CONFIRM_NONE;nav=LCD_NAV_SETTINGS;redraw=true;
                }
            }
        }

        esp_err_t e=ESP_OK;
        if(redraw){
            switch(nav){
            case LCD_NAV_PRISMA: render_home(ui.phase);break;
            case LCD_NAV_GATE: render_gate();break;
            case LCD_NAV_SETTINGS: render_settings(settings_page);break;
            case LCD_NAV_CONFIRM: render_confirm(confirm,last_result);break;
            case LCD_NAV_PHOTO: render_photo();break;
            case LCD_NAV_FILES: render_files();break;
            }
            e=present_once();
            last_redraw=now;
        }else if(nav==LCD_NAV_PRISMA&&change==UI_CHANGE_ADD_GLITCH){
            accumulate_glitch(ui.glitch_count-1U,(int)tx,(int)ty);
            e=present_region(REFERENCE_SCENE_Y,REFERENCE_SCENE_Y+REFERENCE_SCENE_H);
        }else if(nav==LCD_NAV_PRISMA&&ui_model_animation_step(&ui,now)){
            render_home(ui.phase);
            e=present_region(REFERENCE_SCENE_Y,REFERENCE_SCENE_Y+REFERENCE_SCENE_H);
        }else if(nav==LCD_NAV_FILES&&(uint32_t)(now-last_redraw)>5000U){
            files_refresh();render_files();e=present_once();last_redraw=now;
        }else if(nav==LCD_NAV_PHOTO&&(uint32_t)(now-last_redraw)>1500U){
            render_photo();e=present_once();last_redraw=now;
        }else if(nav==LCD_NAV_SETTINGS&&(uint32_t)(now-last_redraw)>3500U){
            render_settings(settings_page);e=present_once();last_redraw=now;
        }
        if(e!=ESP_OK)ESP_LOGE(TAG,"LCD redraw: %s",esp_err_to_name(e));
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t display_start(void)
{
    s_ready = false;
    frame = heap_caps_malloc(LCD_W * LCD_H * 2U, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!frame) frame = heap_caps_malloc(LCD_W * LCD_H * 2U, MALLOC_CAP_8BIT);
    stripe[0] = heap_caps_malloc(LCD_W * DMA_ROWS * 2U, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    stripe[1] = heap_caps_malloc(LCD_W * DMA_ROWS * 2U, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    dma_done = xSemaphoreCreateCounting(2, 0);
    if (!frame || !stripe[0] || !stripe[1] || !dma_done) return ESP_ERR_NO_MEM;

    esp_err_t e = gpio_set_direction(GPIO_NUM_1, GPIO_MODE_OUTPUT);
    if (e != ESP_OK) return e;
    gpio_set_level(GPIO_NUM_1, 0);

    e = board_spi_start();
    if (e != ESP_OK) return e;
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = 42, .cs_gpio_num = 45, .pclk_hz = 40000000,
        .lcd_cmd_bits = 8, .lcd_param_bits = 8, .spi_mode = 0,
        .trans_queue_depth = 2, .on_color_trans_done = color_done
    };
    e = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &io);
    if (e != ESP_OK) return e;

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16
    };
    e = esp_lcd_new_panel_st7789(io, &panel_cfg, &panel);
    if (e != ESP_OK) return e;
    if ((e = esp_lcd_panel_reset(panel)) != ESP_OK) return e;
    if ((e = esp_lcd_panel_init(panel)) != ESP_OK) return e;
    if ((e = esp_lcd_panel_mirror(panel, false, false)) != ESP_OK) return e;
    if ((e = esp_lcd_panel_swap_xy(panel, false)) != ESP_OK) return e;
    if ((e = esp_lcd_panel_invert_color(panel, true)) != ESP_OK) return e;
    if ((e = esp_lcd_panel_disp_on_off(panel, true)) != ESP_OK) return e;

    render_home(0);
    e = present_once();
    if (e != ESP_OK) return e;
    gpio_set_level(GPIO_NUM_1, 1);
    s_ready = true;

    /* Keep framebuffer/DMA stripes for actual animation and swipe navigation. */
    if (xTaskCreatePinnedToCore(display_task, "prisma_display", 4096, NULL,
                                3, NULL, 1) != pdPASS) {
        s_ready = false;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "PRISMA SYSTEM hidden lateral gate + five settings screens; accumulated glitch preserved");
    return ESP_OK;
}
