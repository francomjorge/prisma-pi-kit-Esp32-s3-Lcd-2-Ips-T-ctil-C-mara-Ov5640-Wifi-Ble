#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "app.h"

#define LOGICAL_PATH_MAX 220
#define QUERY_MAX 640
#define COPY_BUF 4096

static const char *TAG = "CS005_FILES";

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode(char *s)
{
    char *r = s, *w = s;
    while (*r) {
        if (*r == '+') { *w++ = ' '; ++r; }
        else if (*r == '%' && r[1] && r[2]) {
            int a = hexval(r[1]), b = hexval(r[2]);
            if (a >= 0 && b >= 0) { *w++ = (char)((a << 4) | b); r += 3; }
            else *w++ = *r++;
        } else *w++ = *r++;
    }
    *w = 0;
}

static void json_escape(const char *src, char *dst, size_t cap)
{
    size_t j = 0;
    if (!cap) return;
    for (size_t i = 0; src && src[i] && j + 2 < cap; ++i) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"' || c == '\\') { dst[j++]='\\'; dst[j++]=(char)c; }
        else if (c == '\n') { dst[j++]='\\'; dst[j++]='n'; }
        else if (c == '\r') { dst[j++]='\\'; dst[j++]='r'; }
        else if (c == '\t') { dst[j++]='\\'; dst[j++]='t'; }
        else if (c >= 32) dst[j++] = (char)c;
    }
    dst[j] = 0;
}

static bool logical_path_valid(const char *p)
{
    if (!p || p[0] != '/' || strlen(p) > LOGICAL_PATH_MAX) return false;
    if (strchr(p, '\\')) return false;
    const char *s = p;
    while (*s) {
        if ((unsigned char)*s < 32) return false;
        ++s;
    }
    s = p;
    while (*s) {
        while (*s == '/') ++s;
        const char *start = s;
        while (*s && *s != '/') ++s;
        size_t len = (size_t)(s - start);
        if (len == 2 && start[0] == '.' && start[1] == '.') return false;
    }
    return true;
}

static esp_err_t resolve_path(const char *volume, const char *logical, char *out, size_t cap)
{
    if (!logical_path_valid(logical)) return ESP_ERR_INVALID_ARG;
    const char *root = storage_volume_root(volume);
    if (!root) return ESP_ERR_INVALID_STATE;
    int n = snprintf(out, cap, "%s%s", root, logical);
    return (n > 0 && (size_t)n < cap) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static bool query_value(httpd_req_t *req, const char *key, char *out, size_t cap)
{
    size_t len = httpd_req_get_url_query_len(req);
    if (!len || len >= QUERY_MAX) return false;
    char query[QUERY_MAX];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) return false;
    if (httpd_query_key_value(query, key, out, cap) != ESP_OK) return false;
    url_decode(out);
    return true;
}

static esp_err_t read_form(httpd_req_t *req, char *buf, size_t cap)
{
    if (req->content_len <= 0 || (size_t)req->content_len >= cap) return ESP_ERR_INVALID_SIZE;
    int got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, buf + got, req->content_len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) return ESP_FAIL;
        got += n;
    }
    buf[got] = 0;
    return ESP_OK;
}

static bool form_value(const char *body, const char *key, char *out, size_t cap)
{
    if (httpd_query_key_value(body, key, out, cap) != ESP_OK) return false;
    url_decode(out);
    return true;
}

static const storage_volume_state_t *find_volume(const storage_state_t *s, const char *volume)
{
    if (!strcmp(volume, "internal")) return &s->internal;
    if (!strcmp(volume, "sd")) return &s->sd;
    return NULL;
}

static esp_err_t storage_get(httpd_req_t *req)
{
    storage_state_t s = {0}; storage_get_state(&s);
    char json[1800];
    snprintf(json, sizeof(json),
        "{\"schema\":\"prisma.storage.v1\",\"shared_spi_ready\":%s,\"policy\":{\"file_count_quota\":false,\"directory_entry_limit\":false,\"max_open_files_per_volume\":%u,\"auto_format\":false},"
        "\"internal\":{\"available\":%s,\"mounted\":%s,\"writable\":%s,\"fs\":\"%s\",\"mount\":\"%s\",\"total_bytes\":%llu,\"used_bytes\":%llu,\"free_bytes\":%llu,\"error\":\"%s\"},"
        "\"sd\":{\"available\":%s,\"mounted\":%s,\"writable\":%s,\"fs\":\"%s\",\"mount\":\"%s\",\"total_bytes\":%llu,\"used_bytes\":%llu,\"free_bytes\":%llu,\"pins\":{\"sclk\":39,\"mosi\":38,\"miso\":40,\"cs\":41},\"error\":\"%s\"}}",
        s.shared_spi_ready?"true":"false", PRISMA_STORAGE_MAX_OPEN_FILES,
        s.internal.available?"true":"false",s.internal.mounted?"true":"false",s.internal.writable?"true":"false",s.internal.fs,s.internal.mount,
        (unsigned long long)s.internal.total_bytes,(unsigned long long)s.internal.used_bytes,(unsigned long long)s.internal.free_bytes,s.internal.error,
        s.sd.available?"true":"false",s.sd.mounted?"true":"false",s.sd.writable?"true":"false",s.sd.fs,s.sd.mount,
        (unsigned long long)s.sd.total_bytes,(unsigned long long)s.sd.used_bytes,(unsigned long long)s.sd.free_bytes,s.sd.error);
    httpd_resp_set_type(req,"application/json");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,json);
}

static esp_err_t list_get(httpd_req_t *req)
{
    char volume[16]={0}, logical[LOGICAL_PATH_MAX+1]={0}, fs_path[256];
    if (!query_value(req,"volume",volume,sizeof(volume))) return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"volume required");
    if (!query_value(req,"path",logical,sizeof(logical))) strlcpy(logical,"/",sizeof(logical));
    if (resolve_path(volume,logical,fs_path,sizeof(fs_path)) != ESP_OK) return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"invalid path or unmounted volume");
    if (!storage_lock(3000)) return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"filesystem busy");
    DIR *dir = opendir(fs_path);
    if (!dir) { storage_unlock(); return httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"directory not found"); }

    httpd_resp_set_type(req,"application/json");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    char v_e[40], p_e[480]; json_escape(volume,v_e,sizeof(v_e)); json_escape(logical,p_e,sizeof(p_e));
    char head[600]; snprintf(head,sizeof(head),"{\"volume\":\"%s\",\"path\":\"%s\",\"entries\":[",v_e,p_e);
    httpd_resp_send_chunk(req,head,HTTPD_RESP_USE_STRLEN);

    bool first=true;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (!strcmp(de->d_name,".") || !strcmp(de->d_name,"..")) continue;
        char child[512];
        if (snprintf(child,sizeof(child),"%s%s%s",fs_path,(!strcmp(logical,"/")?"":"/"),de->d_name) >= (int)sizeof(child)) continue;
        struct stat st={0};
        bool have_stat = stat(child,&st)==0;
        bool is_dir = have_stat && S_ISDIR(st.st_mode);
        char name_e[300]; json_escape(de->d_name,name_e,sizeof(name_e));
        char item[520];
        snprintf(item,sizeof(item),"%s{\"name\":\"%s\",\"type\":\"%s\",\"size\":%llu,\"mtime\":%lld}",
                 first?"":",",name_e,is_dir?"dir":"file",
                 (unsigned long long)(have_stat&&!is_dir?(uint64_t)st.st_size:0),
                 (long long)(have_stat?st.st_mtime:0));
        if (httpd_resp_send_chunk(req,item,HTTPD_RESP_USE_STRLEN)!=ESP_OK) break;
        first=false;
    }
    closedir(dir);
    storage_unlock();
    httpd_resp_send_chunk(req,"]}",2);
    return httpd_resp_send_chunk(req,NULL,0);
}

static const char *mime_for(const char *path)
{
    const char *dot = strrchr(path,'.');
    if (!dot) return "application/octet-stream";
    if (!strcasecmp(dot,".jpg")||!strcasecmp(dot,".jpeg")) return "image/jpeg";
    if (!strcasecmp(dot,".png")) return "image/png";
    if (!strcasecmp(dot,".gif")) return "image/gif";
    if (!strcasecmp(dot,".webp")) return "image/webp";
    if (!strcasecmp(dot,".bmp")) return "image/bmp";
    if (!strcasecmp(dot,".json")) return "application/json";
    if (!strcasecmp(dot,".html")||!strcasecmp(dot,".htm")) return "text/html; charset=utf-8";
    if (!strcasecmp(dot,".css")) return "text/css; charset=utf-8";
    if (!strcasecmp(dot,".js")) return "application/javascript; charset=utf-8";
    if (!strcasecmp(dot,".txt")||!strcasecmp(dot,".log")||!strcasecmp(dot,".md")||!strcasecmp(dot,".csv")||!strcasecmp(dot,".ini")||!strcasecmp(dot,".cfg")||!strcasecmp(dot,".yaml")||!strcasecmp(dot,".yml")) return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

static void safe_filename(const char *path, char out[96])
{
    const char *name = strrchr(path,'/'); name = name?name+1:path;
    size_t j=0;
    for(size_t i=0;name[i]&&j<94;i++) if(name[i]!='"'&&name[i]!='\r'&&name[i]!='\n') out[j++]=name[i];
    out[j]=0;
}

static esp_err_t download_get(httpd_req_t *req)
{
    char volume[16]={0}, logical[LOGICAL_PATH_MAX+1]={0}, inline_s[8]={0}, fs_path[256];
    if(!query_value(req,"volume",volume,sizeof(volume))||!query_value(req,"path",logical,sizeof(logical))) return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"volume/path required");
    if(resolve_path(volume,logical,fs_path,sizeof(fs_path))!=ESP_OK) return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"invalid path or unmounted volume");
    query_value(req,"inline",inline_s,sizeof(inline_s));
    if(!storage_lock(3000)) return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"filesystem busy");
    FILE *f=fopen(fs_path,"rb");
    if(!f){storage_unlock();return httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"file not found");}
    httpd_resp_set_type(req,mime_for(fs_path));
    char filename[96],disp[160];safe_filename(logical,filename);snprintf(disp,sizeof(disp),"%s; filename=\"%s\"",inline_s[0]=='1'?"inline":"attachment",filename);
    httpd_resp_set_hdr(req,"Content-Disposition",disp);
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    char buf[COPY_BUF];size_t n;esp_err_t e=ESP_OK;
    while((n=fread(buf,1,sizeof(buf),f))>0){e=httpd_resp_send_chunk(req,buf,n);if(e!=ESP_OK)break;}
    fclose(f);storage_unlock();
    if(e==ESP_OK)e=httpd_resp_send_chunk(req,NULL,0);
    return e;
}

static esp_err_t upload_post(httpd_req_t *req)
{
    char volume[16]={0}, logical[LOGICAL_PATH_MAX+1]={0}, dest[256], temp[300];
    if(!query_value(req,"volume",volume,sizeof(volume))||!query_value(req,"path",logical,sizeof(logical))) return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"volume/path required");
    if(req->content_len<0||resolve_path(volume,logical,dest,sizeof(dest))!=ESP_OK) return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"invalid path or unmounted volume");
    storage_state_t ss={0};storage_get_state(&ss);const storage_volume_state_t *v=find_volume(&ss,volume);if(!v||!v->mounted)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"volume not mounted");
    if((uint64_t)req->content_len>v->free_bytes){httpd_resp_set_status(req,"507 Insufficient Storage");return httpd_resp_sendstr(req,"not enough free space");}
    if(!storage_lock(5000))return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"filesystem busy");
    struct stat st={0};if(stat(dest,&st)==0&&S_ISDIR(st.st_mode)){storage_unlock();return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"destination is directory");}
    snprintf(temp,sizeof(temp),"%s.prisma-%08lx.part",dest,(unsigned long)esp_random());
    FILE *f=fopen(temp,"wb");if(!f){storage_unlock();return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"cannot create temp file");}
    char buf[COPY_BUF];int remaining=req->content_len;bool ok=true;
    while(remaining>0){int want=remaining>(int)sizeof(buf)?(int)sizeof(buf):remaining;int n=httpd_req_recv(req,buf,want);if(n==HTTPD_SOCK_ERR_TIMEOUT)continue;if(n<=0||fwrite(buf,1,n,f)!=(size_t)n){ok=false;break;}remaining-=n;}
    fflush(f);fclose(f);
    if(!ok||remaining!=0){remove(temp);storage_unlock();return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"upload interrupted");}
    remove(dest);
    if(rename(temp,dest)!=0){remove(temp);storage_unlock();return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"cannot finalize upload");}
    storage_unlock();
    ESP_LOGI(TAG,"Uploaded %s:%s bytes=%d",volume,logical,req->content_len);
    httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,"{\"ok\":true}");
}

static esp_err_t simple_form_paths(httpd_req_t *req,char *body,size_t body_cap,char *volume,size_t vcap,char *path,size_t pcap)
{
    esp_err_t e=read_form(req,body,body_cap);if(e!=ESP_OK)return e;
    if(!form_value(body,"volume",volume,vcap)||!form_value(body,"path",path,pcap))return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}

static esp_err_t mkdir_post(httpd_req_t *req)
{
    char body[640],volume[16],logical[LOGICAL_PATH_MAX+1],fs[256];
    if(simple_form_paths(req,body,sizeof(body),volume,sizeof(volume),logical,sizeof(logical))!=ESP_OK||resolve_path(volume,logical,fs,sizeof(fs))!=ESP_OK)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"invalid volume/path");
    if(!storage_lock(3000))return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"filesystem busy");
    int rc=mkdir(fs,0775);storage_unlock();if(rc!=0&&errno!=EEXIST)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"mkdir failed");
    httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,"{\"ok\":true}");
}

static esp_err_t delete_post(httpd_req_t *req)
{
    char body[640],volume[16],logical[LOGICAL_PATH_MAX+1],fs[256];
    if(simple_form_paths(req,body,sizeof(body),volume,sizeof(volume),logical,sizeof(logical))!=ESP_OK||!strcmp(logical,"/")||resolve_path(volume,logical,fs,sizeof(fs))!=ESP_OK)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"invalid volume/path");
    if(!storage_lock(3000))return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"filesystem busy");
    struct stat st={0};int rc=-1;if(stat(fs,&st)==0)rc=S_ISDIR(st.st_mode)?rmdir(fs):remove(fs);storage_unlock();
    if(rc!=0)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"delete failed; directories must be empty");
    httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,"{\"ok\":true}");
}

static esp_err_t rename_post(httpd_req_t *req)
{
    char body[800],volume[16]={0},from[LOGICAL_PATH_MAX+1]={0},to[LOGICAL_PATH_MAX+1]={0},src[256],dst[256];
    if(read_form(req,body,sizeof(body))!=ESP_OK||!form_value(body,"volume",volume,sizeof(volume))||!form_value(body,"from",from,sizeof(from))||!form_value(body,"to",to,sizeof(to)))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"volume/from/to required");
    if(!strcmp(from,"/")||resolve_path(volume,from,src,sizeof(src))!=ESP_OK||resolve_path(volume,to,dst,sizeof(dst))!=ESP_OK)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"invalid path");
    if(!storage_lock(3000))return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"filesystem busy");int rc=rename(src,dst);storage_unlock();if(rc!=0)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"rename failed");
    httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,"{\"ok\":true}");
}

static esp_err_t copy_post(httpd_req_t *req)
{
    char body[1000],sv[16]={0},sp[LOGICAL_PATH_MAX+1]={0},dv[16]={0},dp[LOGICAL_PATH_MAX+1]={0},src[256],dst[256],tmp[300];
    if(read_form(req,body,sizeof(body))!=ESP_OK||!form_value(body,"source_volume",sv,sizeof(sv))||!form_value(body,"source_path",sp,sizeof(sp))||!form_value(body,"dest_volume",dv,sizeof(dv))||!form_value(body,"dest_path",dp,sizeof(dp)))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"source/destination required");
    if(resolve_path(sv,sp,src,sizeof(src))!=ESP_OK||resolve_path(dv,dp,dst,sizeof(dst))!=ESP_OK)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"invalid path or volume");
    if(!storage_lock(5000))return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"filesystem busy");
    struct stat st={0};if(stat(src,&st)!=0||!S_ISREG(st.st_mode)){storage_unlock();return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"source must be a file");}
    storage_state_t ss={0};storage_get_state(&ss);const storage_volume_state_t *v=find_volume(&ss,dv);if(!v||v->free_bytes<(uint64_t)st.st_size){storage_unlock();httpd_resp_set_status(req,"507 Insufficient Storage");return httpd_resp_sendstr(req,"not enough free space");}
    FILE *in=fopen(src,"rb");if(!in){storage_unlock();return httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"source not found");}
    snprintf(tmp,sizeof(tmp),"%s.prisma-%08lx.part",dst,(unsigned long)esp_random());FILE *out=fopen(tmp,"wb");if(!out){fclose(in);storage_unlock();return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"cannot create destination");}
    char buf[COPY_BUF];size_t n;bool ok=true;while((n=fread(buf,1,sizeof(buf),in))>0){if(fwrite(buf,1,n,out)!=n){ok=false;break;}}fclose(in);fflush(out);fclose(out);
    if(!ok){remove(tmp);storage_unlock();return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"copy failed");}
    remove(dst);if(rename(tmp,dst)!=0){remove(tmp);storage_unlock();return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"copy finalize failed");}storage_unlock();
    httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,"{\"ok\":true}");
}

static esp_err_t internal_init_post(httpd_req_t *req)
{
    esp_err_t e=storage_internal_initialize();if(e!=ESP_OK)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,esp_err_to_name(e));
    httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,"{\"ok\":true,\"mounted\":true}");
}

static esp_err_t sd_retry_post(httpd_req_t *req)
{
    esp_err_t e=storage_sd_retry();if(e!=ESP_OK)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,esp_err_to_name(e));
    httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,"{\"ok\":true,\"mounted\":true}");
}

esp_err_t files_web_register(httpd_handle_t server)
{
    const httpd_uri_t routes[]={
        {.uri="/api/v1/storage",.method=HTTP_GET,.handler=storage_get},
        {.uri="/api/v1/storage/internal/init",.method=HTTP_POST,.handler=internal_init_post},
        {.uri="/api/v1/storage/sd/retry",.method=HTTP_POST,.handler=sd_retry_post},
        {.uri="/api/v1/files/list",.method=HTTP_GET,.handler=list_get},
        {.uri="/api/v1/files/download",.method=HTTP_GET,.handler=download_get},
        {.uri="/api/v1/files/upload",.method=HTTP_POST,.handler=upload_post},
        {.uri="/api/v1/files/mkdir",.method=HTTP_POST,.handler=mkdir_post},
        {.uri="/api/v1/files/delete",.method=HTTP_POST,.handler=delete_post},
        {.uri="/api/v1/files/rename",.method=HTTP_POST,.handler=rename_post},
        {.uri="/api/v1/files/copy",.method=HTTP_POST,.handler=copy_post}
    };
    for(size_t i=0;i<sizeof(routes)/sizeof(routes[0]);++i){esp_err_t e=httpd_register_uri_handler(server,&routes[i]);if(e!=ESP_OK)return e;}
    ESP_LOGI(TAG,"Storage/files API registered: dual-volume browser + streamed transfers");
    return ESP_OK;
}
