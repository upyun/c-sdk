/*
 * pengwu <wp.4163196@gmail.com>
 * */
#ifndef UPYUN_H
#define UPYUN_H
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct upyun_s upyun_t;
typedef size_t (*UPYUN_CONTENT_CALLBACK) (char *ptr, size_t size, size_t nmemb,
                                          void *userdata);

#define UPYUN_MAX_FILE_NAME_LEN 1024
#define UPYUN_MAX_HEADER_LEN 512
#define UPYUN_MAX_FILE_TYPE_LEN 20

typedef enum
{
    UPYUN_RET_OK = 0,
    UPYUN_RET_INVALID_PARAMS,
    UPYUN_RET_NOT_INITED,
    UPYUN_RET_FAIL,
    UPYUN_RET_URL_TOO_LONG,
    UPYUN_RET_INVALID_URL,
    UPYUN_RET_HTTP_FAIL,
}upyun_ret_e;

typedef struct upyun_gmkerl_s
{
    char* url;
    char* type;
    char* value;
    int quality;
    /* "true" or "false". */
    char* unsharp;
    char* thumbnail;
    /* "true" or false. */
    char* exif_switch;
    /* auto / 90 / 180 / 270 */
    char* rotate;
    /* x,y,width,height like (0,0,100,200) */
    char* crop;
}upyun_gmkerl_t;

typedef enum
{
   UPYUN_ED_AUTO = 0,
   UPYUN_ED_TELECOM,
   UPYUN_ED_CNC,
   UPYUN_ED_CTT
}upyun_endpoint_e;

typedef struct upyun_config_s
{
    const char* user;
    const char* passwd;
    int debug;
    upyun_endpoint_e endpoint;
}upyun_config_t;

typedef struct upyun_upload_info_s
{
    int width;
    int height;
    int frames;
    char file_type[UPYUN_MAX_FILE_TYPE_LEN];
}upyun_upload_info_t;

typedef struct upyun_file_info_s
{
    char type[UPYUN_MAX_FILE_TYPE_LEN];
    long size;
    time_t date;
}upyun_file_info_t;

typedef struct upyun_usage_info_s
{
    long usage;
}upyun_usage_info_t;

typedef struct upyun_dir_item_s
{
   char file_name[UPYUN_MAX_FILE_NAME_LEN];
    /* 0 FILE, 1 folder. */
   int file_type;
   long file_size;
   time_t date;
   struct upyun_dir_item_s* next;
}upyun_dir_item_t;

typedef struct upyun_http_header_s
{
    char name[UPYUN_MAX_HEADER_LEN];
    char value[UPYUN_MAX_HEADER_LEN];

    struct upyun_http_header_s* next;
}upyun_http_header_t;

typedef enum
{
    UPYUN_HTTP_METHOD_GET = 0,
    UPYUN_HTTP_METHOD_HEAD,
    UPYUN_HTTP_METHOD_POST,
    UPYUN_HTTP_METHOD_PUT,
    UPYUN_HTTP_METHOD_DELETE,
}upyun_http_method_e;

typedef enum
{
    UPYUN_CONTENT_FILE = 0,
    UPYUN_CONTENT_STR,
}upyun_content_type_e;

typedef struct upyun_content_s
{
    upyun_content_type_e type;
    size_t len;
    int md5;

    union
    {
        FILE* fp;
        char* data;
    }u;
}upyun_content_t;

typedef struct upyun_request_s
{
    upyun_http_method_e method;
    int timeout;
    const char* path;
    const upyun_http_header_t* headers_in;
    int need_headers_out;
    upyun_http_header_t* headers_out;

    const upyun_content_t* upload_content;
    UPYUN_CONTENT_CALLBACK content_callback;
    void* content_ctx;

    /* http response code, zero if unset. */
    int status;
}upyun_request_t;

upyun_ret_e upyun_global_init();
upyun_ret_e upyun_global_cleanup();

/* helper function to free memory. */
void upyun_http_headers_free(upyun_http_header_t* header);
void upyun_dir_items_free(upyun_dir_item_t* items);

upyun_t* upyun_create(const upyun_config_t* config);
upyun_ret_e upyun_destroy(upyun_t* thiz);

upyun_ret_e upyun_set_timeout(upyun_t* thiz, int timeout);
/*
 *  upload a local file to remote specified by [path].
 *  param [path]:
 *      starts by '/', along with bucket name and file path.
 *      eg: /demobucket/upload.png
 *      note: if the path is a directory, it MUST end with '/'
 *
 *  param [content]:
 *      specify a positive content->len is a must.
 *      two ways of storing the content, a FILE* pointer, or a string in memory.
 *
 *  param [gmkerl]:
 *      the additional parameter to make the server process with your uploaded image.
 *      it can be NULL if you just want to upload the image without processing.
 *      for more information about the gmkerl, -->
 *      [http://wiki.upyun.com/index.php?title=HTTP_REST_API%E6%8E%A5%E5%8F%A3#.E5.9B.BE.E7.89.87.E5.A4.84.E7.90.86.E6.8E.A5.E5.8F.A3]
 *
 *  param [info]:
 *      if not NULL, then the uploaded image information will be filled here if expected response received.
 *
 *  param [http_status]:
 *      if not NULL, the http status from server will be filled here,
 *      note that if an occurs in the half way of HTTP, the status will be zero.
 */
upyun_ret_e upyun_upload_file(upyun_t* thiz, const char* path,
        const upyun_content_t* content, const upyun_gmkerl_t* gmkerl,
        upyun_upload_info_t* info, int* http_status);

/*  download file from remote specified by [path].
 *  param [path]: read notes above.
 *  param [callback]:
 *      This function gets called by upyun library as soon as there is data received that needs to be saved.
 *      The size of the data pointed to by ptr is size multiplied with nmemb, it will not be zero terminated.
 *      Return the number of bytes actually taken care of. If that amount differs from the amount passed to your function,
 *      it'll signal an error to the library
 *  param [user_data]:
 *      Data pointer to pass to the [callback] function
 *
 *  param [http_status]: read notes above.
 *
 */
upyun_ret_e upyun_download_file(upyun_t* thiz, const char* path,
        UPYUN_CONTENT_CALLBACK callback, void* user_data, int* http_status);

/*  get file info from remote specified by [path]
 *  param [path] / param [http_status]: read notes above.
 *  param [info]: the file information details.
 */
upyun_ret_e upyun_get_fileinfo(upyun_t* thiz, const char* path,
                               upyun_file_info_t* info, int* http_status);

/*  remove file from remote specified by path.
 */
upyun_ret_e upyun_remove_file(upyun_t* thiz, const char* path,
                              int* http_status);

/*  mkdir on remote.
 *  param [automkdir]:
 *      if true, then we will automatically create the directory if parent directory does not exist.
 */
upyun_ret_e upyun_make_dir(upyun_t* thiz, const char* path, int automkdir,
                           int* http_status);

/*  get the file items below a dir on remote specified by path.
 *  param [result]:
 *      if not NULL, you will get a single linked list of upyun_dir_item_t*,
 *      make sure calling upyun_dir_items_free(*reslut) after use.
 */
upyun_ret_e upyun_read_dir(upyun_t* thiz, const char* path,
                           upyun_dir_item_t** result, int* http_status);

/*  get the usage of bucket specified by path.
 *  path eg: /demobucket/
 *
 * */
upyun_ret_e upyun_get_usage(upyun_t* thiz, const char* path,
                            upyun_usage_info_t* usage, int* http_status);

/*use upyun_remove_file instead.*/
/* upyun_ret_e upyun_remove_dir(upyun_t* thiz, const char* url); */

/*
 * if all APIs above still cannot apply to your application, try this.
 * note: if you are interested with the response header, you should release the
 * memory [request->headers_out] it point to  after use.
 *
 * */
upyun_ret_e upyun_request(upyun_t* thiz, upyun_request_t* request);

#ifdef __cplusplus
}
#endif

#endif /*upyun*/
