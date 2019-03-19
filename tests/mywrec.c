#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>

#include <liblightnvm.h>
#include <liblightnvm_spec.h>

static int be_id = NVM_BE_ANY;
static int seed = 0;
static char nvm_dev_path[NVM_DEV_PATH_LEN] = "/dev/nvme0n1";
static struct nvm_dev *dev;
static const struct nvm_geo *geo;

#define SRC 0
#define DST 1
#define NCHUNKS 2

static int setup(void)
{
	srand(seed);

	dev = nvm_dev_openf(nvm_dev_path, be_id);
	if (!dev) {
		return -1;
	}

	geo = nvm_dev_get_geo(dev);
	//nvm_dev_pr(dev);
	size_t io_nsectr = nvm_dev_get_ws_opt(dev);
	printf("optimum sectors: %lu\n", io_nsectr);
	return 0;
}

static int teardown(void)
{
	geo = NULL;
	nvm_dev_close(dev);

	return 0;
}

int cmd_copy(int cmd_opt)
{
	// 获取最佳写入扇区数
	const size_t io_nsectr = nvm_dev_get_ws_opt(dev);
	// 一个chunk中的字节数
	size_t bufs_nbytes = geo->l.nsectr * geo->l.nbytes;
	struct nvm_buf_set *bufs = NULL;
	struct nvm_addr chunks[NCHUNKS];	//N个chunk地址数组
	int res = -1;

	// 获取NCHUNKS个空闲chunk地址数组
	if (nvm_cmd_rprt_arbs(dev, NVM_CHUNK_STATE_FREE, NCHUNKS, chunks)) {
		printf("nvm_cmd_rprt_arbs\n");
		goto exit;
	}

	// 申请缓冲区，大小为一个chunk的字节数
	bufs = nvm_buf_set_alloc(dev, bufs_nbytes, 0);
	if (!bufs) {
		printf("nvm_buf_set_alloc fail \n");
		goto exit;
	}
	nvm_buf_set_fill(bufs);	// 填充缓冲区

	// 循环总长为一个chunk的sector数，步长是最佳写入扇区数
	for (size_t sectr = 0; sectr < geo->l.nsectr; sectr += io_nsectr) {
		// sec步长与sector的字节相乘，得到最佳写入字节数
		const size_t buf_ofz = sectr * geo->l.nbytes;
		struct nvm_addr src[io_nsectr];	//扇区地址数组

		// 填入地址信息，chunk号和扇区号
		for (size_t idx = 0; idx < io_nsectr; ++idx) {
			src[idx] = chunks[SRC];	// 0
			src[idx].l.sectr = sectr + idx;
		}

		// 一次写入io_nsectr个扇区的数据，数据在bufs->write中随机填写
		res = nvm_cmd_write(dev, src, io_nsectr,
				    bufs->write + buf_ofz, NULL,
				    cmd_opt, NULL);
		if (res < 0) {
			printf("nvm_cmd_write fail\n");
			goto exit;
		}
	}
	printf("write down\n");

	// copy函数，将上面0号chunk的数据复制到1号chunk，内部实现没懂，感觉没实现，会返回-1，输出fail
	for (size_t sectr = 0; sectr < geo->l.nsectr; sectr += io_nsectr) {
		struct nvm_addr src[io_nsectr];
		struct nvm_addr dst[io_nsectr];

		for (size_t idx = 0; idx < io_nsectr; ++idx) {
			src[idx] = chunks[SRC];
			src[idx].l.sectr = sectr + idx;

			dst[idx] = chunks[DST];
			dst[idx].l.sectr = sectr + idx;
		}

		//这里调用了绑定的函数，具体实现没找到，不知道实现没有，只有个返回-1的函数
		if (res < 0) {
			printf("nvm_cmd_copy fail\n");
			goto exit;
		}
	}
	printf("copy done\n");
	
	// 读取1号chunk的数据
	for (size_t sectr = 0; sectr < geo->l.nsectr; sectr += io_nsectr) {
		const size_t buf_ofz = sectr * geo->l.nbytes;
		struct nvm_addr dst[io_nsectr];

		for (size_t idx = 0; idx < io_nsectr; ++idx) {
			dst[idx] = chunks[DST];
			dst[idx].l.sectr = sectr + idx;
		}

		res = nvm_cmd_read(dev, dst, io_nsectr,
				   bufs->read + buf_ofz, NULL,
				   cmd_opt, NULL);
		if (res < 0) {
			printf("nvm_cmd_read fail\n");
			goto exit;
		}
	}
	printf("read 1 chunk done\n");

	size_t t;
	if (t = nvm_buf_diff(bufs->read, bufs->write, bufs->nbytes)) {
		printf("buffer mismatch, bufs_nbytes=%lu, diff_bytes=%lu\n", bufs_nbytes, t);
		for(int i = 0; i < 26; ++i){
			putchar(bufs->write[i]);
		}
		putchar('\n');
		for(int i = 0; i < 26; ++i){
			putchar(bufs->read[i]);
		}
		putchar('\n');
		goto exit;
	}
	else{
		printf("buffer match\n");
	}

	res = 0;

exit:
	nvm_buf_set_free(bufs);
	return res;
}

int main(int argc, char **argv)
{
	int err = 0;

	if(setup()){
		perror("setup fail\n");
		return 0;
	}
	// match
    cmd_copy(NVM_CMD_SCALAR);
	putchar('\n');
	// VECTOR dont macth 
    cmd_copy(NVM_CMD_VECTOR);

	teardown();

	return err;
}
