#include <err.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/types.h>
#include "i2c-dev.h"
#include "log.h"
#include "pack.h"

#define I2C_BUFFER_MAX 8192



//*********************************************************************************
// Transfer I2C data.
//*********************************************************************************

static int i2c_transfer( unsigned int addr,
                         const char *to_write, size_t to_write_len,
                         char *to_read, size_t to_read_len)
{
  struct i2c_rdwr_ioctl_data data;
  struct i2c_msg msgs[2];
  
  int i2c_fd;
  
  
  
  i2c_fd = open("/dev/i2c-1", O_RDWR);
  if (i2c_fd < 0) err(EXIT_FAILURE, "open %s", "/dev/i2c-1");
  
  msgs[0].addr = addr;
  msgs[0].flags = 0;
  msgs[0].len = to_write_len;
  msgs[0].buf = (uint8_t *) to_write;

  msgs[1].addr = addr;
  msgs[1].flags = I2C_M_RD;
  msgs[1].len = to_read_len;
  msgs[1].buf = (uint8_t *) to_read;

  if (to_write_len != 0)
    data.msgs = &msgs[0];
  else
    data.msgs = &msgs[1];

  data.nmsgs = (to_write_len != 0 && to_read_len != 0) ? 2 : 1;

  int rc = ioctl(i2c_fd, I2C_RDWR, &data);
  
  // Close Port
  close(i2c_fd);

  if (rc < 0)
    return 0;
  else
    return 1;
}

//*********************************************************************************
// Send an ok pack response with a data buffer to stdout.
//*********************************************************************************
static void send_ok_data(uint8_t *buf, uint16_t len)
{
  struct pack_map *res = pack_map_new();
  pack_set_str(res, "status", "ok");
  pack_set_int(res, "len",    len);
  pack_set_buf(res, "data",   buf, len);
  if (pack_write(stdout, res) < 0) log_err("ngcubeslib: send_ok_data failed");
  pack_map_free(res);
}

//*********************************************************************************
// Send an error pack response to stdout.
//*********************************************************************************
static void send_err(char *msg)
{
  struct pack_map *res = pack_map_new();
  pack_set_str(res, "status", "err");
  pack_set_str(res, "msg",    msg);
  if (pack_write(stdout, res) < 0) log_err("ngcubeslib: send_err failed");
  pack_map_free(res);
}

//*********************************************************************************
// Send an ok pack response to stdout.
//*********************************************************************************
static void send_ok()
{
  struct pack_map *res = pack_map_new();
  pack_set_str(res, "status", "ok");
  if (pack_write(stdout, res) < 0) log_err("ngcubeslib: send_ok failed");
  pack_map_free(res);
}


//*********************************************************************************
// Read I2C data.
//*********************************************************************************
static void on_read_i2c(struct pack_map *req)
{
  // debug
  char *d = pack_debug(req);
  //log_debug("ngcubeslib: on_read_i2c %s", d);
  free(d);

  char addr[1];
  
  uint8_t i2c = pack_get_int(req, "i2c");
  addr[0] = pack_get_int(req, "addr");
  uint16_t len = pack_get_int(req, "len");

  // check inputs
  if (i2c > 127) { send_err("invalid 'i2c' field"); return; }
  if (addr[0] > 127) { send_err("invalid 'addr' field"); return; }
  if (len <= 1 || len > I2C_BUFFER_MAX) { send_err("invalid 'len' field"); return; }

  char data[I2C_BUFFER_MAX];
  if (i2c_transfer(i2c, addr, 1, data, len))
    send_ok_data((uint8_t*)data, len);
  else
    send_err("read_i2c failed");
}

//*********************************************************************************
// Write data.
//*********************************************************************************
static void on_write_i2c(struct pack_map *req)
{
  // debug
  char *d = pack_debug(req);
  //log_debug("ngcubeslib: on_write_i2c %s", d);
  free(d);
  
  char addr[1];

  uint8_t i2c = pack_get_int(req, "i2c");
  addr[0]  = pack_get_int(req, "addr");
  uint16_t len  = pack_get_int(req, "len") + 1;
  uint8_t *buf =  pack_get_buf(req, "data");

  char data[I2C_BUFFER_MAX];
  
  // check inputs
  if (addr[0] > 127) { send_err("invalid 'addr' field"); return; }
  if (i2c > 127) { send_err("invalid 'i2c' field"); return; }  
  if (len <= 1 || len > I2C_BUFFER_MAX) { send_err("invalid 'len' field"); return; }
  if (data == NULL) { send_err("missing or invalid 'data' field"); return; }
  
  data[0] = addr[0];
  for (int i = 0; i < sizeof(buf);i++)
  {
		data[i+1] = (char)(buf[i]);
  }

  if (i2c_transfer(i2c, data, len, 0, 0))
    send_ok();
  else
    send_err("write_i2c failed");
}


//*********************************************************************************
// Status check.
//*********************************************************************************
static void on_status(struct pack_map *req)
{
  // debug
  char *d = pack_debug(req);
  //log_debug("ngcubeslib: on_status %s", d);
  free(d);

  send_ok();
}

//*********************************************************************************
// Callback to process an incoming Fantom request.
// Returns -1 if process should exit, or 0 to continue.
//*********************************************************************************
static int on_proc_req(struct pack_map *req)
{
  char *op = pack_get_str(req, "op");

  if (strcmp(op, "read_i2c")   == 0) { on_read_i2c(req);   return 0; }
  if (strcmp(op, "write_i2c")  == 0) { on_write_i2c(req);  return 0; }
  if (strcmp(op, "status") == 0) { on_status(req); return 0; }
  if (strcmp(op, "exit")   == 0) { return -1; }

  log_err("ngcubeslib: unknown op '%s'", op);
  return 0;
}


//*********************************************************************************
// Main loop
//*********************************************************************************


int main(int argc, char *argv[])
{
	struct pack_buf *buf = pack_buf_new();
	
	//log_debug("ngcubeslib: open");
	
	for(;;)
    {
		struct pollfd fdset[1];
		fdset[0].fd = STDIN_FILENO;
		fdset[0].events = POLLIN;
		fdset[0].revents = 0;

		// wait for stdin message
		int rc = poll(fdset, 1, -1);
		if (rc < 0)
		{
			// Retry if EINTR
			if (errno == EINTR) continue;
			log_fatal("poll");
		}

		// read message
		if (pack_read(stdin, buf) < 0)
		{
			log_err("ngcubeslib: pack_read failed");
			pack_buf_clear(buf);
		}
		else if (buf->ready)
		{
			struct pack_map *req = pack_decode(buf->bytes);
			int r = on_proc_req(req);
			pack_map_free(req);
			pack_buf_clear(buf);
			if (r < 0) break;
		}		
	}// for
	//log_debug("ngcubeslib: bye-bye");
	return 0;
}