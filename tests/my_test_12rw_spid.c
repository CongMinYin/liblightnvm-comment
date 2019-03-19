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

// 1.2版本的读写，测试速率，
void ewr_s12_1addr(int use_meta)
{
	char *buf_w = NULL, *buf_r = NULL, *meta_w = NULL, *meta_r = NULL;
	//一个lun的plane数目乘以一个page的sector数目
	const int naddrs = geo->nplanes * geo->nsectors;	
	struct nvm_addr addrs[naddrs];
	struct nvm_addr blk_addr[32] = { 0 };
	struct nvm_ret ret;
	ssize_t res;
	size_t buf_w_nbytes,  buf_r_nbytes;
	int pmode = NVM_FLAG_PMODE_SNGL;
	int failed = 1;
	struct timeval start, end;

	//gettimeofday( &end, NULL );
	//printf("alloc block diff: %ld\n", (end.tv_sec - start.tv_sec) * 1000000 + end.tv_usec - start.tv_usec);

	buf_w_nbytes = naddrs * geo->sector_nbytes;	//buf的字节数，
	buf_r_nbytes = geo->sector_nbytes;	//每个扇区字节数，用于读

	// 申请总字节数的空间，用于写
	buf_w = nvm_buf_alloc(dev, buf_w_nbytes, NULL);	// Setup buffers
	if (!buf_w) {
		printf("nvm_buf_alloc");
		goto out;
	}
	nvm_buf_fill(buf_w, buf_w_nbytes);	//填上随机数

	// 申请一个扇区的字节数，用于读
	buf_r = nvm_buf_alloc(dev, buf_r_nbytes, NULL);
	if (!buf_r) {
		printf("nvm_buf_alloc");
		goto out;
	}

	// 获得空闲地址blk_addr，获取的地址是块block
	gettimeofday( &start, NULL );
	for(int i = 0; i < 32; ++i){
	// 地址赋值，此处应该是0，所以是else
	if (pmode) {
		addrs[0].ppa = blk_addr[i].ppa;
	} else {
		for (size_t pl = 0; pl < geo->nplanes; ++pl) {
			addrs[pl].ppa = blk_addr[i].ppa;

			addrs[pl].g.pl = pl;
		}
	}

	if (nvm_cmd_gbbt_arbs(dev, NVM_BBT_FREE, 1, &blk_addr[i])) {
		printf("nvm_cmd_gbbt_arbs");
		goto out;
	}
	// 先擦除，如果是获取的空闲块，在femu中应该不用擦除
	//gettimeofday( &start, NULL );
	res = nvm_cmd_erase(dev, addrs, pmode ? 1 : geo->nplanes, NULL, pmode,
			    &ret);
	if (res < 0) {
		printf("Erase failure");
		goto out;
	}
	//gettimeofday( &end, NULL );
	//printf("erase block diff: %ld\n", (end.tv_sec - start.tv_sec) * 1000000 + end.tv_usec - start.tv_usec);

	// 外层循环geo->npages是一个block的page数量
	// 内层循环naddr是一个lun的plane数目乘以一个page的sector数目
	// 最后的结果是写满1个lun，目的可能是并行，plane可以并行，并行度为一个lun的plane数
	// 一次写命令写入naddr的扇区数目
	// plane和lun的关系应该是横竖交叉的关系，都可以并行，并非包含
	//gettimeofday( &start, NULL );
	for (size_t pg = 0; pg < geo->npages; ++pg) {
		for (int i = 0; i < naddrs; ++i) {
			addrs[i].ppa = blk_addr[i].ppa;

			addrs[i].g.pg = pg;
			addrs[i].g.sec = i % geo->nsectors;
			addrs[i].g.pl = (i / geo->nsectors) % geo->nplanes;
		}
		res = nvm_cmd_write(dev, addrs, naddrs, buf_w,
				     use_meta ? meta_w : NULL, pmode, &ret);
		if (res < 0) {
			printf("Write failure");
			goto out;
		}
	}
	}
	gettimeofday( &end, NULL );
	printf("write size:%lu MB diff: %ld\n", 32*geo->npages*buf_w_nbytes/1024/1024, (end.tv_sec - start.tv_sec) * 1000000 + end.tv_usec - start.tv_usec);

  // 读取是三层循环，plane层循环放中间也是为了并行性
	gettimeofday( &start, NULL );
	for(int i = 0; i < 32; ++i){
	for (size_t pg = 0; pg < geo->npages; ++pg) {
		for (size_t pl = 0; pl < geo->nplanes; ++pl) {
			for (size_t sec = 0; sec < geo->nsectors; ++sec) {
				struct nvm_addr addr;

				addr.ppa = blk_addr[i].ppa;
				addr.g.pg = pg;
				addr.g.pl = pl;
				addr.g.sec = sec;

				memset(buf_r, 0, buf_r_nbytes);

				// 读取一个sector
				res = nvm_cmd_read(dev, &addr, 1, buf_r,
						    use_meta ? meta_r : NULL, pmode, &ret);
				if (res < 0) {
					printf("Read failure");
					goto out;
				}

			}
		}
	}
	}
	gettimeofday( &end, NULL );
	printf("read size:%lu MB diff: %ld\n", 32*geo->npages*geo->nplanes*geo->nsectors*buf_r_nbytes/1024/1024,  (end.tv_sec - start.tv_sec) * 1000000 + end.tv_usec - start.tv_usec);

	failed = 0;
	printf("Success\n");
  return ;
out:
	printf("Fail out.\n");
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
	ewr_s12_1addr(0);
	printf("\n");
	//printf("----------test2-----------\n"); 
	//ewr_s12_1addr(1);

	teardown();

	return err;
}
