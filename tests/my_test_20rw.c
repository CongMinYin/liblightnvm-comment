#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>
#include <sys/time.h>
#include <liblightnvm.h>
#include <liblightnvm_spec.h>

static int be_id = NVM_BE_ANY;
static int seed = 0;
static char nvm_dev_path[NVM_DEV_PATH_LEN] = "/dev/nvme0n1";
static struct nvm_dev *dev;
static const struct nvm_geo *geo;

static int setup(void)
{
	srand(seed);

	dev = nvm_dev_openf(nvm_dev_path, be_id);
	if (!dev) {
		return -1;
	}

	geo = nvm_dev_get_geo(dev);

	return 0;
}

static int teardown(void)
{
	geo = NULL;
	nvm_dev_close(dev);

	return 0;
}

static long long ustime(void) {
    struct timeval tv;
    long long ust;

    gettimeofday(&tv, NULL);
    ust = ((long long)tv.tv_sec) * 1000000;
    ust += tv.tv_usec;
    return ust;
}

/**
 * @param use_meta Set to 1 to allocate, write, read and verify meta-data buffer
 * @param erase_mode set to NVM_CMD_SCALAR or to NVM_CMD_VECTOR
 */
static void ewr(int use_meta, int erase_cmd_mode)
{
	const int naddrs = nvm_dev_get_ws_min(dev);	 //最小写入扇区数
	struct nvm_buf_set *bufs = NULL;
	struct nvm_ret ret;
	struct nvm_addr chunk_addr = { .val = 0 };
	ssize_t res;

	if (nvm_cmd_rprt_arbs(dev, NVM_CHUNK_STATE_FREE, 1, &chunk_addr)) {
		printf("nvm_cmd_rprt_arbs");
		goto failure;
	}

	bufs = nvm_buf_set_alloc(dev, naddrs * geo->l.nbytes,
				 use_meta ? naddrs * geo->l.nbytes_oob : 0);
	if (!bufs) {
		printf("nvm_buf_set_alloc");
		goto failure;
	}
	nvm_buf_set_fill(bufs);

	// Erase the chunk
	// 擦除这一组chunk地址
	res = nvm_cmd_erase(dev, &chunk_addr, 1, NULL, erase_cmd_mode, &ret);
	if (res < 0) {
		printf("Erase failure");
		goto failure;
	}

	// Write the chunk
	// 写操作
    long long start = ustime();
	for (size_t sectr = 0; sectr < geo->l.nsectr; sectr += naddrs) {
		struct nvm_addr addr = chunk_addr;	//浅拷贝

		addr.l.sectr = sectr;

		// 写入地址addr组，最小扇区数naddrs，写入缓冲区bufs->write，
		// 元数据写入缓冲区bufs->write_meta，写入模式NVM_CMD_SCALAR，返回值
        
		res = nvm_cmd_write(dev, &addr, naddrs, bufs->write,
				    bufs->write_meta, NVM_CMD_SCALAR, &ret);
		if (res < 0) {
			printf("Write failure");
			goto failure;
		}
	}
    printf("write chunk size:%lu,TIME DIFF:%llu\n",  geo->l.nsectr * geo->l.nbytes , ustime()-start);
    start = ustime();
    res = nvm_cmd_write(dev, &chunk_addr, naddrs, bufs->write,
				    bufs->write_meta, NVM_CMD_SCALAR, &ret);
    printf("write single size:%lu,bufs->nbytes:%lu naddrs:%d TIME DIFF:%llu\n",  naddrs * geo->l.nbytes,bufs->nbytes, naddrs, ustime()-start);               

	// Read
	//读操作
    start = ustime();
	for (size_t sectr = 0; sectr < geo->l.nsectr; sectr += naddrs) {
		struct nvm_addr addr = chunk_addr;	//依旧是刚才写的地址

		size_t buf_diff = 0;
		size_t meta_diff = 0;

		addr.l.sectr = sectr;

		// 重置读缓冲区
		memset(bufs->read, 0, bufs->nbytes);	///< Reset read buffers
		//有元数据缓冲区，则重置元数据缓冲区
		if (use_meta)
			memset(bufs->read_meta, 0, bufs->nbytes_meta);

		// 读操作，参数类似写
		res = nvm_cmd_read(dev, &addr, naddrs, bufs->read,
				   bufs->read_meta, NVM_CMD_SCALAR, &ret);
		if (res < 0) {
			printf("Read failure: command error\n");
			goto failure;
		}

		// 比较读写内容是否相同
        /*
		buf_diff = nvm_buf_diff(bufs->read, bufs->write, bufs->nbytes);
		if (buf_diff) {
			printf("Read failure: buffer mismatch");
		}

		if (use_meta) {
			meta_diff = nvm_buf_diff(bufs->read_meta,
						 bufs->write_meta,
						 bufs->nbytes_meta);
			if (meta_diff) {
				printf("Read failure: meta mismatch");		
			}
		}

		if (buf_diff || meta_diff) {
			printf("buf_diff || meta_diff");
			goto failure;
		}*/
	}
    printf("read chunk size:%lu,TIME DIFF:%llu\n",  geo->l.nsectr * geo->l.nbytes , ustime()-start);
    start = ustime();
    res = nvm_cmd_read(dev, &chunk_addr, naddrs, bufs->read,
				   bufs->read_meta, NVM_CMD_SCALAR, &ret);
    printf("read single size:%lu,TIME DIFF:%llu\n",  naddrs * geo->l.nbytes , ustime()-start);               


	printf("Success\n");

failure:
	nvm_buf_set_free(bufs);
}


int main(int argc, char **argv)
{
	int err = 0;

	if(setup()){
		perror("setup fail\n");
		return 0;
	}

	// 测试printf的速度
	printf("----------test1-----------\n"); 
	ewr(0, NVM_CMD_SCALAR);
	printf("\n");
	//printf("----------test2-----------\n"); 
	//ewr_s12_1addr(1);

	teardown();

	return err;
}
