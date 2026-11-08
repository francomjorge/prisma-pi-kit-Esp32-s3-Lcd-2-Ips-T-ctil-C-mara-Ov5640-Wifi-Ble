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

static const char *TAG = "CS010Y_LCD";
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
    /* The hidden SYSTEM GATE is not part of PRISMA's page indicators. */
}


/* Exactly the original PRISMA SYSTEM navigation: a hidden lateral gate,
 * two actionable buttons, then five internal settings screens.
 * This is independent of the PRISMA HOME page indicators.
 */
#define SETTINGS_COUNT 5
#define SETTINGS_DOTS_Y 307
typedef enum { LCD_NAV_PRISMA=0, LCD_NAV_GATE, LCD_NAV_SETTINGS, LCD_NAV_CONFIRM } lcd_nav_t;
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
        centered_text(70,"CS010Y PLATFORM",2,wh);
        snprintf(b,sizeof(b),"UP %llu SEC",(unsigned long long)(esp_timer_get_time()/1000000ULL));
        centered_text(115,b,2,wh);
        snprintf(b,sizeof(b),"HEAP %u KB",(unsigned)(esp_get_free_heap_size()/1024U));
        centered_text(151,b,2,wh);
        /* Same runtime log mode used by Web ADMIN / DIAGNOSTICS. */
        snprintf(b,sizeof(b),"LOGS %s",platform_log_mode_name());
        centered_button(197,35,b);
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
            }else if(action==UI_EVENT_TAP){
                change=ui_model_input(&ui,action,now);
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
                }else if(ty>=190&&ty<240&&settings_page==4){
                    /* Physical button cycles the SAME state reflected in Web. */
                    platform_log_mode_t current=platform_log_mode_get();
                    platform_log_mode_t next=(platform_log_mode_t)
                        ((current + 1U) % (PLATFORM_LOG_DEBUG + 1U));
                    if(platform_log_mode_set(next)==ESP_OK)redraw=true;
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
            }
            e=present_once();
            last_redraw=now;
        }else if(nav==LCD_NAV_PRISMA&&change==UI_CHANGE_ADD_GLITCH){
            accumulate_glitch(ui.glitch_count-1U,(int)tx,(int)ty);
            e=present_region(REFERENCE_SCENE_Y,REFERENCE_SCENE_Y+REFERENCE_SCENE_H);
        }else if(nav==LCD_NAV_PRISMA&&ui_model_animation_step(&ui,now)){
            render_home(ui.phase);
            e=present_region(REFERENCE_SCENE_Y,REFERENCE_SCENE_Y+REFERENCE_SCENE_H);
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
