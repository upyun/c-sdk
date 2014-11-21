# 又拍云 C SDK

[![Build Status](https://travis-ci.org/upyun/c-sdk.svg?branch=master)](https://travis-ci.org/upyun/c-sdk)

又拍云存储 C SDK，基于 [又拍云存储 HTTP REST API](http://docs.upyun.com/api/rest_api/) 开发。


依赖: [libcurl](https://github.com/bagder/curl)

###安装
```
make
make test
```

### 操作接口说明
#### upyun 生命周期
```
upyun_global_init ==>
upyun_create ==>
upyun_upload_file/upload_download_file/... ==>
upyun_destroy ==>
upyun_global_cleanup
```

#### 全局init/全局cleanup
##### 函数原型
```
upyun_ret_e upyun_global_init();
upyun_ret_e upyun_global_cleanup();

```

##### 说明
init函数在创建所有upyun_t实例前调用
cleanup在所有upyun_t实例销毁后调用

#### upyun_t实例创建
##### 函数原型
```
upyun_t* upyun_create(const upyun_config_t* config);
```

##### 调用演示
```
upyun_config_t conf = {0};
conf.user = "demouser";
conf.passwd = "demopasswd";
conf.endpoint = UPYUN_ED_TELECOM;
conf.debug = 1
upyun_t* u = upyun_create(&conf);
```

##### 说明
在调用具体API之前，必须先创建一个upyun_t实例，每次调用API接口时，都要将此创建的实例作为入口参数传入。
upyun_config_t指定了该新创建实例的一些配置信息，其中包括：
* debug. 不为0时，打印调试信息
* user.  空间用户名
* passwd. 空间密码
* endpoint. 指定该实例使用的接入点，请根据网络情况自行选择。可选接入点包括：
    * 自动选择 (UPYUN_ED_AUTO)
    * 电信     (UPYUN_ED_TELECOM)
    * 网通     (UPYUN_ED_CNC)
    * 铁通     (UPYUN_ED_CTT)

另外请注意，默认网络超时时间设为永不超时，若需设置超时，请通过upyun_set_timeout 设定。

##### 返回值
若成功则返回新创建的实例，否则返回空指针。

#### 上传文件
##### 函数原型
```
upyun_ret_e upyun_upload_file(upyun_t* thiz, const char* path,
const upyun_content_t* content, const upyun_gmkerl_t* gmkerl,
upyun_upload_info_t* info, int* http_status);
```

##### 调用演示
```
upyun_content_t content = {0};
content.type = UPYUN_CONTENT_FILE;
content.u.fp = fopen("1.jpg", "rb");
content.len = file_size("1.jpg");
content.md5 = 1;

upyun_upload_info_t upload_info = {0};
int status = 0;
ret = upyun_upload_file(u, "/pengwu-img/1.jpg", &content, NULL, &upload_info, &status);
fclose(conten.u.fp);
```

##### 说明
* *path*由‘/’开始，并且由空间名和文件路径名组成。
* *content* 指定上传内容，包含以下字段：
  * *content.len* 指定上传内容的长度，值必须大于0
  * *content.type* 指定上传内容的类型，目前只支持FILE*和char*两种类型，分别从文件和内存中读取内容。
  * *content.md5* 当为1时，会对上传内容生成 *Content-MD5* http 头校验码
* *gmkerl* 作图参数，传值为NULL时则表示只是单纯上传文件。具体填值请参考 [API 文档](http://docs.upyun.com/api/rest_api/#_4)
* *info* 若传入的*info*不为NULL，则在图片文件成功上传后，sdk内部会修改*info*字段值。
* *status* 服务器的响应http状态码。

##### 返回值
成功时返回UPYUN_RET_OK, 其他情况请参考upyun_ret_e结构体定义。

#### 下载文件
##### 函数原型
```
upyun_ret_e upyun_download_file(upyun_t* thiz, const char* path,
        UPYUN_CONTENT_CALLBACK callback, void* user_data, int* http_status);
```

##### 调用演示
```
size_t write_data(void *ptr, size_t size, size_t nmemb, void *stream)
{
        int written = fwrite(ptr, size, nmemb, (FILE *)stream);
        return written;
}

FILE* fp = NULL;
if((fp=fopen("download.jpg", "w"))==NULL)
{
    exit(1);
}

ret = upyun_download_file(u, "/pengwu-img/1.jpg", (UPYUN_CONTENT_CALLBACK)write_data, fp, &status);
assert(ret == UPYUN_RET_OK && status == 200);
```

#### 读取目录文件信息
##### 函数原型
```
upyun_ret_e upyun_read_dir(upyun_t* thiz, const char* path, upyun_dir_item_t** result, int* http_status);
```

##### 调用演示
```
upyun_dir_item_t* item = NULL;
int status = 0;
upyun_ret_e ret = upyun_read_dir(thiz, "/pengwu-img/", &item, &status);
assert(ret == UPYUN_RET_OK && status == 200);

upyun_dir_item_t* s_item = item;
for(; item!=NULL; item=item->next)
{
    printf("dir item: name %s, type %d, size %d date %d\n",
            item->file_name, item->file_type, item->file_size, item->date);
}
upyun_dir_items_free(s_item);
```

##### 说明
如果成功获取文件信息, 且传入的二级指针*result*不为NULL，则在收到服务器正确响应之后，sdk内部会生成一个链表，*result指向的是链表的首节点， 同时注意在result在使用完后需通过upyun_dir_items_free释放内存。

#### 获取文件信息
##### 函数原型
```
upyun_ret_e upyun_get_fileinfo(upyun_t* thiz, const char* path, upyun_file_info_t* info, int* http_status)
```

##### 调用演示
```
upyun_file_info_t info = {0};
ret = upyun_get_fileinfo(thiz, path, &info, &status);
assert(ret == UPYUN_RET_OK && status == 200);
printf("file size: %d, file type: %s, file date: %d\n", info.size, info.type, info.date);
```

##### 说明
入口参数*info*若不为NULL，则在服务器成功返回后，sdk内部会将文件信息填入info。

#### 创建目录
##### 函数原型
```
upyun_ret_e upyun_make_dir(upyun_t* thiz, const char* path, int automkdir, int* http_status);
```

##### 调用演示
```
sprintf(path, "/pengwu-img/%s/", path);
ret = upyun_make_dir(thiz, path, 0, &status);
assert(ret == UPYUN_RET_OK && status == 200);
```

##### 说明
入口参数path必须以'/'结束，automkdir设为1时，会自动创建不存在的父级目录。

#### 删除文件
##### 函数原型
```
upyun_ret_e upyun_remove_file(upyun_t* thiz, const char* path, int* http_status);
```
##### 调用演示
```
ret = upyun_remove_file(u, "/pengwu-img/1.jpg", &status);
assert(ret == UPYUN_RET_OK && status == 200);
```

##### 说明
可同时用于删除目录或者文件，当删除的目标是目录时，*path*必须以'/'结束。


#### 获取空间信息
##### 函数原型
```
upyun_ret_e upyun_get_usage(upyun_t* thiz, const char* path, upyun_usage_info_t* usage, int* http_status);
```

##### 调用演示
```
ret = upyun_get_usage(u, "/pengwu-img/", &status);
assert(ret == UPYUN_RET_OK && status == 200);
```

##### 说明
*path*为指定的空间名，并由'/'开始，由'/'结束。

#### upyun_request
##### 函数原型
```
upyun_ret_e upyun_request(upyun_t* thiz, upyun_request_t* request);
```

##### 调用演示
```
upyun_request_t request = {0};
request.status = 0;
request.method = UPYUN_HTTP_METHOD_DELETE;
request.path = path;
request.headers_in = NULL;
request.need_headers_out = 1;
request.timeout = 30;
upyun_ret_e ret = upyun_request(thiz, &request);
assert(ret == UPYUN_RET_OK && request.status == 200);
if(request.headers_out) upyun_http_headers_free(request.headers_out);
```
##### 说明
之前的API已经能够覆盖大部分的需求，但如果调用者由更具体的要求，则可以使用此API.
需要特别注意的是，如果request.need_headers_out设为1，则在收到服务器端响应后，sdk会内部创建一个链表，request.headers_out指向表头, 所以在使用完后，需通过upyun_http_headers_free释放该链表的内存。

