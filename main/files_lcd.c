#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "app.h"

/* LCD read-only paginated view of the SAME mounted volumes used by Web FILES.
 * Bounded memory: four visible rows; no file contents loaded in RAM.
 * Filesystem mutex shared with uploads, downloads and photo capture.
 */
esp_err_t files_lcd_list(const char *volume,const char *logical,
                         uint32_t offset,files_lcd_page_t *out)
{
    if(!out)return ESP_ERR_INVALID_ARG;
    memset(out,0,sizeof(*out));
    if(!logical||logical[0]!='/'||strstr(logical,"..")||
       strlen(logical)>FILES_LCD_PATH_MAX)return ESP_ERR_INVALID_ARG;
    const char *root=storage_volume_root(volume);
    if(!root)return ESP_ERR_INVALID_STATE;
    char dirpath[160];
    int n=snprintf(dirpath,sizeof(dirpath),"%s%s",root,logical);
    if(n<0||n>=(int)sizeof(dirpath))return ESP_ERR_INVALID_SIZE;
    if(!storage_lock(1000))return ESP_ERR_TIMEOUT;
    DIR *dir=opendir(dirpath);
    if(!dir){storage_unlock();return ESP_ERR_NOT_FOUND;}
    struct dirent *entry;
    uint32_t seen=0;
    while((entry=readdir(dir))){
        if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."))continue;
        if(seen>=offset && out->count<FILES_LCD_ROWS){
            files_lcd_entry_t *row=&out->rows[out->count++];
            strlcpy(row->name,entry->d_name,sizeof(row->name));
            char child[240];
            int used=snprintf(child,sizeof(child),"%s%s%s",dirpath,
                 dirpath[strlen(dirpath)-1]=='/'?"":"/",entry->d_name);
            if(used>0&&used<(int)sizeof(child)){
                struct stat st={0};
                if(stat(child,&st)==0){
                    row->directory=S_ISDIR(st.st_mode);
                    row->bytes=row->directory?0:(uint32_t)st.st_size;
                }
            }
        }
        if(seen<UINT32_MAX)seen++;
    }
    out->total=seen;
    closedir(dir);
    storage_unlock();
    return ESP_OK;
}
