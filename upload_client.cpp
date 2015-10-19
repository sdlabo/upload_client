#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errors.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <termios.h>
#include <time.h>
#include <float.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <vector>
#include <complex>
#include <zlib.h>

using namespace std;
typedef std::complex<double> sdlab_complex;

#define GZ_MODE "wb9f"
FILE *fplog;
char hostname[1024] = {0};
char uploadcgi[1024] = {0};

void load_config()
{
  FILE* fp = fopen("config.ini", "r");
  if(fp == NULL){
    printf("need config.ini file\n");
    printf("hostname=[hostname]\n");
    printf("urlpath=[urlpath]\n");
    exit(-1);
  }

  char line[1024] = {0};

  while(fgets(line, 1024, fp) != NULL){
    char attr[1024] = {0};
    char value[1024] = {0};

    for(int i = 0; i < strlen(line); i++){
      if(line[i] != '='){
        attr[i] = line[i];
      }else{
        attr[i] = 0;
        break;
      }
    }

    int j = 0;
    for(int i = strlen(attr) + 1; i < strlen(line); i++){
      if(line[i] == ' ' || line[i] == '\n' || line[i] == '\r'){
        value[j] = 0;
      }else{
        value[j] = line[i];
      }

      j++;
    }

    if(strcmp(attr, "hostname") == 0){
      strcpy(hostname, value);
    }else if(strcmp(attr, "uploadcgi") == 0){
      strcpy(uploadcgi, value);
    }
  }

  if(strlen(hostname) == 0){
    printf("hostname is not set.\n");
    exit(-1);
  }

  if(strlen(uploadcgi) == 0){
    printf("uploadcgi is not set.\n");
    exit(-1);
  }

  printf("hostname = %s\n", hostname);
  printf("uploadcgi = %s\n", uploadcgi);
}

void compress_gzip(const char *from, const char *to)
{
  sleep(1); //コピー中だとエラーが発生するかも
  FILE* fp = fopen(from, "r");
  if(fp == NULL){
    fprintf(fplog, "fopen error\r\n");
    fflush(fplog);
    perror("fopen");
    exit(1);
  }

  gzFile gf = gzopen(to, GZ_MODE);
  if(gf == NULL){
    fprintf(fplog, "gzopen error\r\n");
    fflush(fplog);
    perror("gzopen");
    exit(1);
  }

  char buf[16384];
  uint64_t sum = 0;

  fprintf(fplog, "begin gzwrite\r\n");
  fflush(fplog);

  printf("start compression.\n");
  while(int ret = fread(buf, 1, sizeof(buf), fp)){
    ret = gzwrite(gf, buf, ret);
  }
  printf("end compression.\n");

  fprintf(fplog, "end gzwrite\r\n");
  fflush(fplog);

  int ret = gzclose(gf);
  if(ret != Z_OK){
    perror("gzclose");
    exit(1);
  }

  fclose(fp);
}

int create_tcp_socket()
{
  int ret = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  return ret;
}

int connect2host(int sock, const char *hostname, int port)
{
  struct sockaddr_in addr;
  addr.sin_family = AF_INET;

  struct hostent *host;
  host = gethostbyname(hostname);
  addr.sin_addr = *(struct in_addr *)(host->h_addr_list[0]);
  addr.sin_port = htons(port);

  int ret = connect(sock, (struct sockaddr *)&addr, sizeof addr);
  return ret;
}

long get_filesize(const char* filename)
{
  struct stat buf;
  stat(filename, &buf);

  return buf.st_size;
}

int send_file(int sock, const char* filename)
{
  FILE *fp = fopen(filename, "r");
  if(fp == NULL){
    return -1;
  }

  int total = 0;
  while(1){
    uint8_t buf[16384];
    int size = fread(buf, sizeof(uint8_t), sizeof(buf), fp);

    if(size <= 0){
      break;
    }
    int ret = send(sock, buf, size, 0);
    if(ret < 0){
      fclose(fp);
      return -1;
    }

    total += size;
    printf(".");
  }
  fclose(fp);

  printf("\ntotal = %d bytes.\n", total);
  return 0;
}

int upload_file(const char *filename)
{
  fprintf(fplog, "create_tcp_socket\r\n");
  fflush(fplog);

  int sock = create_tcp_socket();

  fprintf(fplog, "connect2host\r\n");
  fflush(fplog);

  int ret = connect2host(sock, hostname, 80);
  printf("connect2host = %d\n", ret);
  if(ret != 0){
    perror("connect2host");
    return -1;
  }

  char header[1024];
  char pre[1024];
  char post[1024];
  char boundary[] = "sarulab-at-hamamatsu";

  sprintf(pre, "--%s\r\n", boundary);
  sprintf(pre + strlen(pre), "Content-Disposition: form-data; ");
  sprintf(pre + strlen(pre), "name=\"userfile\"; ");
  sprintf(pre + strlen(pre), "filename=\"%s\"\r\n", filename);
  sprintf(pre + strlen(pre), "Content-Type: application/octet-stream\r\n");
  sprintf(pre + strlen(pre), "Content-Transfer-Encoding: binary\r\n\r\n");

  sprintf(post, "\r\n--%s--\r\n", boundary);

  int filesize = get_filesize(filename);
  int length = filesize + strlen(pre) + strlen(post);

  sprintf(header, "POST %s HTTP/1.0\r\n", uploadcgi);
  sprintf(header + strlen(header), "Host: %s\r\n", hostname);
  sprintf(header + strlen(header), "Content-Type:multipart/form-data; ");
  sprintf(header + strlen(header), "boundary=%s\r\n", boundary);
  sprintf(header + strlen(header), "Content-Length: %d\r\n\r\n", length);

  fprintf(fplog, "send header\r\n");
  fflush(fplog);

  ret = send(sock, header, strlen(header), 0);
  if(ret < 0){
    return -1;
  }

  fprintf(fplog, "send pre\r\n");
  fflush(fplog);

  ret = send(sock, pre, strlen(pre), 0);
  if(ret < 0){
    return -1;
  }

  fprintf(fplog, "send_file\r\n");
  fflush(fplog);

  ret = send_file(sock, filename);
  if(ret < 0){
    return -1;
  }

  fprintf(fplog, "send post\r\n");
  fflush(fplog);

  ret = send(sock, post, strlen(post), 0);
  if(ret < 0){
    return -1;
  }

  fprintf(fplog, "recv reply\r\n");
  fflush(fplog);

  while(1){
    char buf[1024];

    int size = recv(sock, buf, 1024, 0);
    if(size <= 0){
      break;
    }

    buf[size] = 0;
    printf(">> %s\n", buf);
  }

  close(sock);

  return 0;
}

int print_files()
{
  DIR *dir;
  dir = opendir("./files/");
  struct dirent *dp;

  for(dp = readdir(dir); dp != NULL; dp = readdir(dir)){
    if(dp->d_type == DT_REG){
      printf("file? %s\n", dp->d_name);
    }
  }
  closedir(dir);
}

int move_files()
{
  DIR *dir;
  dir = opendir("./files/");
  struct dirent *dp;

  for(dp = readdir(dir); dp != NULL; dp = readdir(dir)){
    if(dp->d_type == DT_REG){
      char src[1024];
      char dst[1024];
      printf("file? %s\n", dp->d_name);
      sprintf(src, "files/%s", dp->d_name);
      sprintf(dst, "done/%s", dp->d_name);
      int ret = rename(src, dst);
      if(ret < 0){
        perror("rename");
      }
    }
  }
  closedir(dir);
}

void check_data()
{
  DIR *dir;
  dir = opendir("./files/");
  struct dirent *dp;

  for(dp = readdir(dir); dp != NULL; dp = readdir(dir)){
    if(dp->d_type == DT_REG){
      char src[1024];
      char dst[1024];

      fprintf(fplog, "02: found! %s\r\n", dp->d_name);
      fflush(fplog);

      int tail = strlen(dp->d_name);
      printf("%c %c\n", dp->d_name[tail - 2], dp->d_name[tail - 1]);
      if(dp->d_name[tail - 2] == 'g' && dp->d_name[tail - 1] == 'z'){
        continue;
      }

      printf("found!: %s\n", dp->d_name);
      sprintf(src, "files/%s", dp->d_name);
      sprintf(dst, "done/%s", dp->d_name);

      char output_file[1024];
      sprintf(output_file, "%s.gz", src);

      fprintf(fplog, "03: start compress_gzip\r\n");
      fflush(fplog);

      compress_gzip(src, output_file);

      fprintf(fplog, "04: start upload_file\r\n");
      fflush(fplog);
      int ret = upload_file(output_file);

      if(ret != -1){
        fprintf(fplog, "05: start rename\r\n");
        fflush(fplog);

        ret = rename(src, dst);
        while(ret != 0){
          sleep(1);
          fprintf(fplog, "05: rename failed. re-renaming\r\n");
          fflush(fplog);
          ret = rename(src, dst);
        }

        fprintf(fplog, "05: end rename %d\r\n", ret);
        fflush(fplog);

        fprintf(fplog, "06: start remove\r\n");
        fflush(fplog);

        ret = remove(output_file);
        while(ret != 0){
          sleep(1);
          fprintf(fplog, "06: remove failed. re-removing\r\n");
          fflush(fplog);
          ret = remove(output_file);
        }

        fprintf(fplog, "06: end remove %d\r\n", ret);
        fflush(fplog);
      }
    }
  }
  closedir(dir);
}


int main()
{
  load_config();
  mkdir("./done",
        S_IRUSR | S_IWUSR | S_IXUSR |
        S_IRGRP | S_IWGRP | S_IXGRP |
        S_IROTH | S_IXOTH | S_IXOTH);

  mkdir("./files",
        S_IRUSR | S_IWUSR | S_IXUSR |
        S_IRGRP | S_IWGRP | S_IXGRP |
        S_IROTH | S_IXOTH | S_IXOTH);

  fplog = fopen("log.txt", "w");

  fprintf(fplog, "01: while loop\r\n");
  fflush(fplog);

  while(1){
    check_data();
    sleep(10);
  }
}
