/*
 * pengwu <wp.4163196@gmail.com>
 * */
#include <time.h>
#include <string.h>
#include <curl/curl.h>
#include <assert.h>
#include <ctype.h>
#include "md5.h"
#include "upyun.h"

#define return_val_if_fail(p, ret) if(!(p)) {return (ret);}
/*{fprintf(stderr, "%s:%d Warning: "#p" failed.\n",\
 *     __func__, __LINE__); return (ret);}*/

#define MAX_BUF_LEN 1024
#define MAX_QUOTED_URL_LEN 1024 * 3 + 1

int s_inited = 0;

struct upyun_s
{
    const upyun_config_t* config;
    int timeout;
    CURL *curl;

};

typedef struct upyun_readdir_ctx_s
{
    upyun_dir_item_t* items;
    char* buf;
    size_t buflen;
    int error;
}upyun_readdir_ctx_t;

typedef struct upyun_usage_ctx_s
{
    char* buf;
    size_t buflen;
}upyun_usage_ctx_t;

/* be consistent with upyun_endpoint_e */
const char* upyun_endpoints[] =
{
    "Host: v0.api.upyun.com",
    "Host: v1.api.upyun.com",
    "Host: v2.api.upyun.com",
    "Host: v3.api.upyun.com"
};

/* be consistent with upyun_http_method_e */
const char* upyun_http_methods[] =
{
    "GET",
    "HEAD",
    "POST",
    "PUT",
    "DELETE"
};

static char* get_token(char** line, const char* delim, char* end_ptr)
{
    if(line == NULL || *line == NULL
            || delim == NULL
            || *line >= end_ptr) return NULL;

    char* p = *line;
    char* p_start = NULL;
    char* ret = NULL;

    /* skip the delim characters. */
    while(*p != '\0' && strchr(delim, *p) && p < end_ptr)
    {
        p++;
    }

    assert(p <= end_ptr);
    if(p == end_ptr)
    {
        ret = NULL;
        goto DONE;
    }

    p_start = p;
    p++;

    /* looking for the delim. */
    while(*p != '\0' && !strchr(delim, *p) && p < end_ptr)
    {
        p++;
    }

    assert(p <= end_ptr);
    if(p == end_ptr)
    {
        if(*p == '\0') ret = p_start;

        goto DONE;
    }
    else
    {
        ret = p_start;
        *p = '\0';
        p++;
    }

DONE:
    *line = p;

    return ret;
}

static void skip_space(char** line, char* end_ptr)
{
    char* p = *line;
    while(*p != '\0' && isspace(*p)  && p < end_ptr) p++;

    *line = p;

    return;
}

static size_t response_header_callback(void *ptr, size_t size,
        size_t nmemb, void *userdata)
{
    upyun_http_header_t** headers = userdata;
    char ccc_buf[1024] = {0};
    memcpy(ccc_buf, ptr, size * nmemb);

    /* there wont be multilines, one line in each call. */
    char* line = ptr;
    char* line_end = line + size * nmemb;

    skip_space(&line, line_end);
    char* name = get_token(&line, ":", line_end);
    if(name == NULL) return size * nmemb;

    skip_space(&line, line_end);
    if(line == line_end) return size * nmemb;

    char* value = get_token(&line, "\r\n", line_end);
    if(value == NULL) return size * nmemb;

    upyun_http_header_t* h = calloc(1, sizeof(upyun_http_header_t));
    if(h == NULL) return size * nmemb;

    strncpy(h->name, name, UPYUN_MAX_HEADER_LEN);
    strncpy(h->value, value, UPYUN_MAX_HEADER_LEN);

    h->next = *headers;
    *headers = h;

    /* printf("New header:[%s]: [%s], %u, %u\n", h->name, h->value, size, */
    /*        nmemb); */
    return size * nmemb;
}

static size_t read_content_callback(void *ptr, size_t size, size_t nmemb,
                                    void *stream)
{
    upyun_content_t* content = (upyun_content_t* )stream;
    size_t nread = 0;

    if(content->type == UPYUN_CONTENT_FILE)
    {
        nread = fread(ptr, size, nmemb, content->u.fp);
    }
    else
    {
        if(content->len == 0) return 0;

        if(content->len <= size * nmemb)
        {
            memcpy(ptr, content->u.data, content->len);
            nread = content->len;
            content->u.data = NULL;
            content->len = 0;
        }
        else
        {
            size_t copy_len = size * nmemb;
            memcpy(ptr, content->u.data, copy_len);
            content->u.data += copy_len;
            content->len -= copy_len;
            nread = copy_len;
        }

        /* printf("curl read %zu bytes.\n", nread); */
    }

    return (curl_off_t) nread;
}

static void upyun_md5(const char* in, int in_len, char* out)
{
    unsigned char decrypt[16];
    MD5_CTX md5;
    MD5Init(&md5);
    MD5Update(&md5, (unsigned char *)in, in_len);
    MD5Final(&md5, decrypt);

    unsigned char * pcur = decrypt;
    const char * hex = "0123456789abcdef";
    char * pout = out;
    int i = 0;
    for(; i < 16; ++i){
        *pout++ = hex[(*pcur>>4)&0xF];
        *pout++ = hex[(*pcur)&0xF];
        pcur++;
    }

    return;
}
static void get_rfc1123_date(char* out, int len)
{
    time_t t = time(NULL);
    struct tm p;

    gmtime_r(&t, &p);
    /* Thu, 29 Aug 2013 05:24:17 GMT */
    strftime(out, len, "%a, %d %b %Y %T GMT", &p);

    return;
}

static void upyun_generate_auth_header(const char* user, const char* passwd,
                                       upyun_http_method_e method,
                                       size_t content_len, const char* uri,
                                       char* out)
{
    /* md5(METHOD & URI & DATE & CONTENT_LENGTH & md5(PASSWORD)) */

    char buf[MAX_BUF_LEN] ={0};
    int size = 0;
    size += sprintf(buf + size, "%s&", upyun_http_methods[method]);
    size += sprintf(buf + size, "%s&", uri);

    char time_buf[100] = {0};
    get_rfc1123_date(time_buf, 100);
    size += sprintf(buf + size, "%s&", time_buf);

    size += sprintf(buf + size, "%zu&", content_len);
    upyun_md5(passwd, strlen(passwd), buf + size);
    size += 32;

    int prefix_size = sprintf(out, "Authorization: UpYun %s:", user);

    /* printf("length: %d %zu, md5 code: %s\n", size, strlen(buf), buf); */
    upyun_md5(buf, size, out + prefix_size);

    return;
}

static void upyun_readdir_parse(upyun_readdir_ctx_t* ctx)
{
    char* p = ctx->buf;
    char* p_end = ctx->buf + ctx->buflen;


    for(;;)
    {
        char* name = get_token(&p, "\t", p_end);
        if(name == NULL) break;

        char* type = get_token(&p, "\t", p_end);
        if(type == NULL) break;

        char* size = get_token(&p, "\t", p_end);
        if(size == NULL) break;

        char* date = get_token(&p, "\n", p_end);
        if(date == NULL) break;

        upyun_dir_item_t* item = calloc(1, sizeof(upyun_dir_item_t));

        if(type[0] == 'N') item->file_type = 0;
        else item->file_type = 1;

        strncpy(item->file_name, name, UPYUN_MAX_FILE_NAME_LEN);
        item->file_size = atol(size);
        item->date = (time_t) atol(date);

        item->next = ctx->items;
        ctx->items = item;
    }

    /* TODO should we check whether or not there is error when parsing? */
    return;
}

static int url_encode(const char* input, char* out, size_t max_len)
{
    const char* p = input;
    char* q = out;
    char hex[] = "0123456789ABCDEF";
    char safe[] = ".-~_/?";

    for(p=input; *p!='\0' && (size_t)(p-input)<max_len; p++)
    {
        char c = *p;

        if ((c >='0' && c <= '9') ||
                (c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                (strchr(safe, c) != NULL))
        {
            *q++ = *p;
        }
        else
        {
            *q++ = '%';
            *q++ = hex[(*p >> 4) & 0xF];
            *q++ = hex[*p & 0xF];
        }
    }

    if((size_t)(p - input) >= max_len && *p != '\0') return 0;

    return 1;
}

static size_t upyun_readdir_content_callback(char *ptr, size_t size,
                                             size_t nmemb, void *userdata)
{
    upyun_readdir_ctx_t* ctx = (upyun_readdir_ctx_t* )userdata;

    ctx->buf = realloc(ctx->buf, size * nmemb + ctx->buflen + 1);
    memcpy(ctx->buf + ctx->buflen, ptr, size * nmemb);
    ctx->buflen += size * nmemb;
    *(ctx->buf + ctx->buflen)  = '\0';

    return size * nmemb;
}

static size_t upyun_usage_content_callback(char *ptr, size_t size, size_t nmemb,
                                           void *userdata)
{
    upyun_usage_ctx_t* ctx = (upyun_usage_ctx_t* )userdata;

    ctx->buf = realloc(ctx->buf, size * nmemb + ctx->buflen + 1);
    memcpy(ctx->buf + ctx->buflen, ptr, size * nmemb);
    ctx->buflen += size * nmemb;
    *(ctx->buf + ctx->buflen)  = '\0';

    /* printf("get usage data: %zu, %s, %d", size * nmemb, ctx->buf, */
    /*        ctx->buflen); */
    return size * nmemb;
}

static upyun_ret_e upyun_request_internal(upyun_t* thiz,
                                          upyun_request_t* request,
                                          struct curl_slist** headers)
{
    return_val_if_fail(s_inited > 0, UPYUN_RET_NOT_INITED);
    return_val_if_fail(thiz != NULL &&
            request != NULL &&
            request->method >= UPYUN_HTTP_METHOD_GET &&
            request->method <= UPYUN_HTTP_METHOD_DELETE &&
            request->path != NULL &&
            request->path[0] == '/', UPYUN_RET_INVALID_PARAMS);

    const upyun_content_t* content = request->upload_content;
    if(content != NULL)
    {
        if(content->type == UPYUN_CONTENT_FILE)
        {
            return_val_if_fail(content->u.fp != NULL
                    && content->len > 0, UPYUN_RET_INVALID_PARAMS);
        }
        else if(content->type == UPYUN_CONTENT_STR)
        {
            return_val_if_fail(content->u.data != NULL
                    && content->len > 0, UPYUN_RET_INVALID_PARAMS);
        }
        else return UPYUN_RET_INVALID_PARAMS;
    }

    struct curl_slist* curl_headers = *headers;
    upyun_ret_e ret = UPYUN_RET_OK;
    CURL *curl = thiz->curl;
    curl_easy_reset(curl);

    upyun_content_t copy_upload_content = {0};

    char url[MAX_QUOTED_URL_LEN] = {0};
    size_t prefix_len = strlen("http://") +
        strlen(upyun_endpoints[thiz->config->endpoint]) - strlen("Host: ");

    sprintf(url, "http://%s",
            upyun_endpoints[thiz->config->endpoint] + sizeof("Host: ") - 1);

    char* quoted_uri = url + prefix_len;
    if(!url_encode(request->path, quoted_uri, MAX_QUOTED_URL_LEN - prefix_len))
    {
        ret = UPYUN_RET_URL_TOO_LONG;
        goto DONE;
    }
    /* printf("full path: %s\n", url); */

    curl_easy_setopt(curl, CURLOPT_URL, url);

    if(request->method == UPYUN_HTTP_METHOD_GET)
    {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    }
    else if(request->method == UPYUN_HTTP_METHOD_HEAD)
    {
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    }
    else if(request->method == UPYUN_HTTP_METHOD_POST)
    {
        /* curl_easy_setopt(curl, CURLOPT_POST, 1L); */
        curl_easy_setopt(curl,CURLOPT_CUSTOMREQUEST,"POST");
    }
    else if(request->method == UPYUN_HTTP_METHOD_PUT)
    {
        curl_easy_setopt(curl, CURLOPT_PUT, 1L);
    }
    else if(request->method == UPYUN_HTTP_METHOD_DELETE)
    {
        curl_easy_setopt(curl,CURLOPT_CUSTOMREQUEST,"DELETE");
    }

    if(request->method == UPYUN_HTTP_METHOD_PUT
            || request->method == UPYUN_HTTP_METHOD_POST)
    {
        if(request->upload_content != NULL)
        {
            copy_upload_content = *(request->upload_content);
            curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_content_callback);
            curl_easy_setopt(curl, CURLOPT_READDATA, &copy_upload_content);
        }
    }

    if(request->need_headers_out)
    {
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &request->headers_out);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, response_header_callback);
    }

    size_t content_len = 0;
    if(request->method == UPYUN_HTTP_METHOD_POST ||
            request->method == UPYUN_HTTP_METHOD_PUT)
    {
        content_len = request->upload_content != NULL
            ? request->upload_content->len : 0;
    }

    char auth_header[MAX_BUF_LEN] = {0};
    upyun_generate_auth_header(thiz->config->user, thiz->config->passwd,
            request->method, content_len, quoted_uri, auth_header);

    curl_headers = curl_slist_append(curl_headers, auth_header);
    curl_headers = curl_slist_append(curl_headers,
            upyun_endpoints[thiz->config->endpoint]);

    char buf[MAX_BUF_LEN] = {0};
    strcpy(buf, "DATE: ");
    get_rfc1123_date(buf + sizeof("DATE: ") - 1, 100);
    curl_headers = curl_slist_append(curl_headers, buf);

    if(request->upload_content)
    {
        curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE,
                (curl_off_t)(request->upload_content->len));
        /*sprintf(buf, "Content-Length: %zu", request->upload_content->len);*/
    }
    else
    {
        curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE,
                (curl_off_t) 0);
        strcpy(buf, "Content-Length: 0");
        curl_headers = curl_slist_append(curl_headers, buf);
    }

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers);

    if(request->content_callback)
    {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, request->content_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, request->content_ctx);
    }

    if(request->timeout <= 0) curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0);
    else curl_easy_setopt(curl, CURLOPT_TIMEOUT, request->timeout);

    /* No signals allowed in case of multithreaded apps */
    /* TODO what if there is problem in dns parsing? */
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    if(thiz->config->debug) curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    else curl_easy_setopt(curl, CURLOPT_VERBOSE, 0);

    CURLcode rv = curl_easy_perform(curl);
    if(rv != CURLE_OK) ret = UPYUN_RET_HTTP_FAIL;

    curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &request->status);
DONE:
    *headers = curl_headers;

    return ret;
}

upyun_ret_e upyun_global_init()
{
    curl_global_init(CURL_GLOBAL_ALL);
    s_inited = 1;

    return UPYUN_RET_OK;
}

upyun_ret_e upyun_global_cleanup()
{
    curl_global_cleanup();
    s_inited = 0;

    return UPYUN_RET_OK;
}

void upyun_dir_items_free(upyun_dir_item_t* items)
{
    if(items == NULL) return;

    upyun_dir_item_t* i = items;
    upyun_dir_item_t* inext = NULL;

    for(;;)
    {
         inext = i->next;

         free(i);

         if(inext == NULL) break;
         else i = inext;
    }

    return;
}

void upyun_http_headers_free(upyun_http_header_t* header)
{
    if(header == NULL) return;

    upyun_http_header_t* h = header;
    upyun_http_header_t* hnext = NULL;

    for(;;)
    {
         hnext = h->next;

         free(h);

         if(hnext == NULL) break;
         else h = hnext;
    }

    return;
}

upyun_t* upyun_create(const upyun_config_t* config)
{
    return_val_if_fail(s_inited > 0, NULL);
    return_val_if_fail(config != NULL &&
            config->user != NULL &&
            config->passwd != NULL &&
            config->endpoint >= UPYUN_ED_AUTO &&
            config->endpoint <= UPYUN_ED_CTT, NULL);

    upyun_t* thiz = calloc(1, sizeof(upyun_t));
    if(thiz == NULL) return NULL;

    thiz->config = config;
    thiz->timeout = -1;
    thiz->curl = curl_easy_init();
    if(thiz->curl == NULL) 
    {
        free(thiz);

        return NULL;
    }

    return thiz;
}

upyun_ret_e upyun_destroy(upyun_t* thiz)
{
    return_val_if_fail(thiz != NULL, UPYUN_RET_INVALID_PARAMS);

    curl_easy_cleanup(thiz->curl);

    free(thiz);

    return UPYUN_RET_OK;
}

upyun_ret_e upyun_set_timeout(upyun_t* thiz, int timeout)
{
    return_val_if_fail(thiz != NULL, UPYUN_RET_INVALID_PARAMS);

    thiz->timeout = timeout;

    return UPYUN_RET_OK;
}

upyun_ret_e upyun_request(upyun_t* thiz, upyun_request_t* request)
{
    char buf[MAX_BUF_LEN] = {0};
    struct curl_slist* curl_headers = NULL;

    if(request->headers_in != NULL)
    {
        const upyun_http_header_t* h = request->headers_in;
        for(; h!=NULL; h=h->next)
        {
            /* TODO complain it. */
            if(h->name == NULL || h->value == NULL) continue;
            if(strlen(h->name) + sizeof(": ")-1
                    + strlen(h->value) > MAX_BUF_LEN)continue;

            snprintf(buf, MAX_BUF_LEN, "%s: %s", h->name, h->value);
            curl_headers = curl_slist_append(curl_headers, buf);
        }
    }

    upyun_ret_e ret = upyun_request_internal(thiz, request, &curl_headers);
    if(curl_headers != NULL) curl_slist_free_all(curl_headers);

    return ret;
}

upyun_ret_e upyun_upload_file(upyun_t* thiz, const char* path,
        const upyun_content_t* content, const upyun_gmkerl_t* gmkerl,
        upyun_upload_info_t* info, int* http_status)
{
    return_val_if_fail(path != NULL && path[0] == '/'
            && path[strlen(path) - 1] != '/'
            && content != NULL, UPYUN_RET_INVALID_PARAMS);

    struct curl_slist* curl_headers = NULL;
    char buf[MAX_BUF_LEN] = {0};

    /* let the upyun server validate the gmkerl. */
    if(gmkerl != NULL)
    {
        if(gmkerl->type != NULL)
        {
            snprintf(buf, MAX_BUF_LEN, "x-gmkerl-type: %s", gmkerl->type);
            curl_headers = curl_slist_append(curl_headers, buf);
        }
        if(gmkerl->value != NULL)
        {
            snprintf(buf, MAX_BUF_LEN, "x-gmkerl-value: %s", gmkerl->value);
            curl_headers = curl_slist_append(curl_headers, buf);
        }
        if(gmkerl->quality >= 1)
        {
            snprintf(buf, MAX_BUF_LEN, "x-gmkerl-quality: %d", gmkerl->quality);
            curl_headers = curl_slist_append(curl_headers, buf);
        }
        if(gmkerl->unsharp != NULL)
        {
            snprintf(buf, MAX_BUF_LEN, "x-gmkerl-unsharp: %s", gmkerl->unsharp);
            curl_headers = curl_slist_append(curl_headers, buf);
        }
        if(gmkerl->thumbnail != NULL)
        {
            snprintf(buf, MAX_BUF_LEN, "x-gmkerl-thumbnail: %s",
                     gmkerl->thumbnail);
            curl_headers = curl_slist_append(curl_headers, buf);
        }
        if(gmkerl->exif_switch != NULL)
        {
            snprintf(buf, MAX_BUF_LEN, "x-gmkerl-exif-switch: %s",
                     gmkerl->exif_switch);
            curl_headers = curl_slist_append(curl_headers, buf);
        }
        if(gmkerl->rotate != NULL)
        {
            snprintf(buf, MAX_BUF_LEN, "x-gmkerl-rotate: %s", gmkerl->rotate);
            curl_headers = curl_slist_append(curl_headers, buf);
        }
        if(gmkerl->crop != NULL)
        {
            snprintf(buf, MAX_BUF_LEN, "x-gmkerl-crop: %s", gmkerl->crop);
            curl_headers = curl_slist_append(curl_headers, buf);
        }
    }

    upyun_request_t request = {0};

    request.status = 0;
    request.method = UPYUN_HTTP_METHOD_PUT;
    request.path = path;
    request.need_headers_out = 1;
    request.timeout = thiz->timeout;
    request.upload_content = content;

    upyun_ret_e ret = upyun_request_internal(thiz, &request, &curl_headers);
    if(curl_headers != NULL) curl_slist_free_all(curl_headers);
    if(ret != UPYUN_RET_OK || request.status > 206)
    {
        if(request.status > 206) ret = UPYUN_RET_HTTP_FAIL;
        goto DONE;
    }

    if(info == NULL) goto DONE;

    info->width = -1; info->height = -1; info-> frames = -1;
    info->file_type[0] = '\0';

    upyun_http_header_t* h = request.headers_out;
    for(; h!=NULL;h=h->next)
    {
        if(strcmp(h->name, "x-upyun-width") == 0
                && h->value != NULL)
        {
            info->width = atoi(h->value);
        }
        else if(strcmp(h->name, "x-upyun-height") == 0
                && h->value != NULL)
        {
            info->height = atoi(h->value);
        }
        else if(strcmp(h->name, "x-upyun-frames") == 0
                && h->value != NULL)
        {
            info->frames = atoi(h->value);
        }
        else if(strcmp(h->name, "x-upyun-file-type") == 0
                && h->value != NULL
                && strlen(h->value) < UPYUN_MAX_FILE_TYPE_LEN)
        {
            strcpy(info->file_type, h->value);
        }
    }

DONE:
    if(http_status) *http_status = request.status;
    upyun_http_headers_free(request.headers_out);

    return ret;
}

/* TODO maybe we should provide some default callbacks */
upyun_ret_e upyun_download_file(upyun_t* thiz, const char* path,
                                UPYUN_CONTENT_CALLBACK callback,
                                void* user_data, int* http_status)
{
    return_val_if_fail(path != NULL && path[0] == '/'
            && path[strlen(path) - 1] != '/', UPYUN_RET_INVALID_PARAMS);

    upyun_request_t request = {0};

    request.status = 0;
    request.method = UPYUN_HTTP_METHOD_GET;
    request.path = path;
    request.headers_in = NULL;
    request.need_headers_out = 0;
    request.timeout = thiz->timeout;
    request.content_callback = callback;
    request.content_ctx = user_data;

    upyun_ret_e ret = upyun_request(thiz, &request);
    if(ret != UPYUN_RET_OK || request.status > 206)
    {
        if(request.status > 206) ret = UPYUN_RET_HTTP_FAIL;
    }

    if(http_status) *http_status = request.status;

    return ret;
}

upyun_ret_e upyun_get_fileinfo(upyun_t* thiz, const char* path,
                               upyun_file_info_t* info, int* http_status)
{
    return_val_if_fail(path != NULL && path[0] == '/',
                       UPYUN_RET_INVALID_PARAMS);

    upyun_request_t request = {0};

    request.status = 0;
    request.method = UPYUN_HTTP_METHOD_HEAD;
    request.path = path;
    request.headers_in = NULL;
    request.need_headers_out = 1;
    request.timeout = thiz->timeout;

    upyun_ret_e ret = upyun_request(thiz, &request);

    if(ret != UPYUN_RET_OK || request.status > 206)
    {
        if(request.status > 206) ret = UPYUN_RET_HTTP_FAIL;
        goto DONE;
    }

    if(info == NULL) goto DONE;

    info->type[0] = '\0';
    info->size = -1;
    info->date = (time_t) -1;
    upyun_http_header_t* h = request.headers_out;
    for(; h!=NULL; h=h->next)
    {
        if(strcmp(h->name, "x-upyun-file-type") == 0)
        {
            if(h->value != NULL) strncpy(info->type, h->value,
                                         UPYUN_MAX_FILE_TYPE_LEN);
        }
        else if(strcmp(h->name, "x-upyun-file-size") == 0)
        {
            if(h->value != NULL) info->size = atoi(h->value);
        }
        else if(strcmp(h->name, "x-upyun-file-date") == 0)
        {
            if(h->value != NULL) info->date = (time_t)atol(h->value);
        }
    }

DONE:
    if(http_status) *http_status = request.status;
    upyun_http_headers_free(request.headers_out);

    return ret;
}

upyun_ret_e upyun_remove_file(upyun_t* thiz, const char* path, int* http_status)
{
    upyun_request_t request = {0};

    request.status = 0;
    request.method = UPYUN_HTTP_METHOD_DELETE;
    request.path = path;
    request.headers_in = NULL;
    request.need_headers_out = 0;
    request.timeout = thiz->timeout;

    upyun_ret_e ret = upyun_request(thiz, &request);

    if(ret != UPYUN_RET_OK || request.status > 206)
    {
        if(request.status > 206) ret = UPYUN_RET_HTTP_FAIL;
    }

    if(http_status) *http_status = request.status;

    return ret;
}


upyun_ret_e upyun_make_dir(upyun_t* thiz, const char* path, int automkdir,
                           int* http_status)
{
    return_val_if_fail(s_inited > 0, UPYUN_RET_NOT_INITED);
    return_val_if_fail(thiz != NULL && path != NULL, UPYUN_RET_INVALID_PARAMS);
    struct curl_slist* curl_headers = NULL;
    char buf[MAX_BUF_LEN] = {0};

    strcpy(buf, "folder: true");
    curl_headers = curl_slist_append(curl_headers, buf);

    if(automkdir) strcpy(buf, "mkdir: true");
    else strcpy(buf, "mkdir: false");
    curl_headers = curl_slist_append(curl_headers, buf);

    upyun_request_t request = {0};

    request.status = 0;
    request.method = UPYUN_HTTP_METHOD_POST;
    request.path = path;
    request.timeout = thiz->timeout;

    upyun_ret_e ret = upyun_request_internal(thiz, &request, &curl_headers);
    if(curl_headers != NULL) curl_slist_free_all(curl_headers);
    if(ret != UPYUN_RET_OK || request.status > 206)
    {
        if(request.status > 206) ret = UPYUN_RET_HTTP_FAIL;
    }

    if(http_status) *http_status = request.status;

    return ret;
}

upyun_ret_e upyun_read_dir(upyun_t* thiz, const char* path,
                           upyun_dir_item_t** result, int* http_status)
{
    return_val_if_fail(s_inited > 0, UPYUN_RET_NOT_INITED);
    return_val_if_fail(thiz != NULL
            && path != NULL
            && path[0] == '/'
            && path[strlen(path)-1] == '/'
            && result != NULL, UPYUN_RET_INVALID_PARAMS);

    upyun_request_t request = {0};

    request.status = 0;
    request.path = path;
    request.method = UPYUN_HTTP_METHOD_GET;
    request.timeout = thiz->timeout;

    request.content_callback = upyun_readdir_content_callback;
    upyun_readdir_ctx_t ctx = {0};
    request.content_ctx = &ctx;

    upyun_ret_e ret = upyun_request(thiz, &request);
    if(ret != UPYUN_RET_OK || request.status > 206)
    {
        if(request.status > 206) ret = UPYUN_RET_HTTP_FAIL;
        goto DONE;
    }

    if(ctx.error)
    {
        ret = UPYUN_RET_HTTP_FAIL;
        *result = NULL;
    }
    else
    {
        upyun_readdir_parse(&ctx);
        *result = ctx.items;
    }

DONE:
    if(ctx.buf != NULL) free(ctx.buf);
    if(http_status) *http_status = request.status;

    return ret;
}

upyun_ret_e upyun_get_usage(upyun_t* thiz, const char* path,
                            upyun_usage_info_t* usage, int* http_status)
{
    return_val_if_fail(thiz != NULL
            && path != NULL
            && path[0] == '/'
            && path[strlen(path)-1] == '/'
            && usage != NULL, UPYUN_RET_INVALID_PARAMS);

    upyun_usage_ctx_t ctx = {0};
    usage->usage = -1;
    upyun_request_t request = {0};

    request.status = 0;
    char uri[MAX_BUF_LEN] = {0};
    int size = snprintf(uri, MAX_BUF_LEN, "%s?usage", path);
    if(size >= MAX_BUF_LEN) return UPYUN_RET_URL_TOO_LONG;

    request.path = uri;
    request.method = UPYUN_HTTP_METHOD_GET;
    request.timeout = thiz->timeout;

    request.content_callback = upyun_usage_content_callback;
    request.content_ctx = &ctx;

    upyun_ret_e ret = upyun_request(thiz, &request);
    if(ret != UPYUN_RET_OK || request.status > 206)
    {
        if(request.status > 206) ret = UPYUN_RET_HTTP_FAIL;
        goto DONE;
    }

    if(ctx.buflen > 0)
    {
        usage->usage = atol(ctx.buf);
    }

DONE:
    if(ctx.buflen) free(ctx.buf);
    if(http_status) *http_status = request.status;

    return ret;
}
