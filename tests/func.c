/* Author:cmyin
 * date:2018-10-17 
 * description: code comment and interface list
 */

// open-channel SSDs组织结构
/*
liblightnvm	    OCSSD 1.2	        OCSSD 2.0
PUGRP	        Channel	Parallel    Unit Group
PUNIT	        LUN / die	        Parallel Unit
CHUNK	        Block	            Chunk
                Plane	
                Page	
SECTR	        Sector	            Logical Block
*/
// 1.2的组织形式比较麻烦，读写地址组织很麻烦，2.0简单很多
// 读写的操作单位都是扇区sector，擦除单位为块chunk
// 主要函数有四个，测试中，每获取一个空闲块随机进行擦除再写读，虚拟机中，第一次使用，应该可以不用先擦除
{
    nvm_cmd_rprt_arbs 
    nvm_cmd_erase 
    nvm_cmd_write
    nvm_cmd_read
}


// 函数描述格式，其中英文描述摘抄自http://lightnvm.io/liblightnvm/capi/index.htm，与liblightnvm.h中注释相同
/* Description:
 * 描述：
 * 输入：    
 * 输出：
 * 返回：
 */

/* 测试内容
 * test_cmd_erase.c改编为my_erase.c 
 * 
 */

/* 注释内容
 * test_intf.c
 * test_cmd_erase.c
 * test_cmd_ewr_scalar.c
 * test_cmd_ewr_vector.c
 * test_cmd_copy.c  //简单易懂，2.0版本
 */

/* 问题记录
 * -[x] nvm_cmd_erase擦除函数中向量和标量模式有什么区别，已解决，是否用参数带出块描述报告
 * -[ ] OOB是什么，跟meta有关
 * 
 * 
 */

//头文件
#include <liblightnvm.h>
#include <liblightnvm_spec.h>

// 打开设备
struct nvm_dev *dev = nvm_dev_openf(nvm_dev_path, be_id);

// 获取设备几何结构
const struct nvm_geo *geo = nvm_dev_get_geo(dev);

// 关闭设备
nvm_dev_close(dev);

/* Description: Find an arbitrary set of ‘naddrs’ chunk-addresses on the given ‘dev’, 
 * in the given chunk state ‘cs’ and store them in the provided ‘addrs’ array.
 * 描述：获取随意一组空闲块地址 chunk
 * 输入：dev：设备
 *      NVM_CHUNK_STATE_FREE：状态码，表明
 *      naddrs：地址个数
 * 输出：chunk_addrs 地址数组
 * 返回：0 -1
 */
int nvm_cmd_rprt_arbs(struct nvm_dev * dev, int cs, int naddrs, struct nvm_addr addrs[])
nvm_cmd_rprt_arbs(dev, NVM_CHUNK_STATE_FREE, naddrs, chunk_addrs)


//最小写入扇区数
const int naddrs = nvm_dev_get_ws_min(dev);	
// 获取最佳写入扇区数
const size_t io_nsectr = nvm_dev_get_ws_opt(dev);

/* Description:Execute an Open-Channel 1.2 erase / Open-Channel 2.0 reset command.
 * 描述：擦除一组块
 * 输入：flags:擦除模式，有两种NVM_CMD_SCALAR，NVM_CMD_VECTOR 标量和向量
 *      向量模式多一个元数据void *NVM_UNUSED(meta)参数，用nvm_spec_rprt_descr结构带回块描述报告
 * 输出：meta 
 * 返回：0 -1
 */
int nvm_cmd_erase(struct nvm_dev * dev, struct nvm_addr addrs[], int naddrs, void * meta, uint16_t flags, struct nvm_ret * ret)
ssize_t res = nvm_cmd_erase(dev, chunk_addrs, naddrs, updated, erase_mode, &ret);   //update为块描述报告结构体，内容为空
res = nvm_cmd_erase(dev, &chunk_addr, 1, erase_meta, 0x0, &ret);

/* Description:
 * 描述：写操作函数
 * 输入：写入地址addr组，最小扇区数naddrs，写入缓冲区bufs->write，
 *      元数据写入缓冲区bufs->write_meta，写入模式NVM_CMD_SCALAR  
 * 输出：ret 
 * 返回：0 -1
 */
int nvm_cmd_write(struct nvm_dev * dev, struct nvm_addr addrs[], int naddrs, const void * data, const void * meta, uint16_t flags, struct nvm_ret * ret)
ssize_t res = nvm_cmd_write(dev, &addr, naddrs, bufs->write, bufs->write_meta, NVM_CMD_SCALAR, &ret);
res = nvm_cmd_write(dev, addrs, naddrs, bufs->write, bufs->write_meta, 0x0, &ret);

/* Description:
 * 描述：读操作函数
 * 输入：同上    
 * 输出：ret
 * 返回：0 -1
 */
int nvm_cmd_read(struct nvm_dev * dev, struct nvm_addr addrs[], int naddrs, void * data, void * meta, uint16_t flags, struct nvm_ret * ret)
ssize_t res = nvm_cmd_read(dev, &addr, naddrs, bufs->read, bufs->read_meta, NVM_CMD_SCALAR, &ret);

/* Description:Executes one or multiple Open-Channel 2.0 get-log-page for chunk-information.
 * 
 * 描述：获取1个chunk信息
 * 输入：dev: Device handle obtained with nvm_dev_open
 *      addr: Pointer to a struct nvm_addr containing the address of a chunk to report about
 *      opt: Reporting options, see enum nvm_spec_chunk_state
 *      ret: Pointer to structure in which to store lower-level status and result    
 * 返回：chunk报告结构 NULL
 */
struct nvm_spec_rprt* nvm_cmd_rprt(struct nvm_dev * dev, struct nvm_addr * addr, int opt, struct nvm_ret * ret)
struct nvm_spec_rprt *rprt = nvm_cmd_rprt(dev, &lun_addr, 0, NULL)

/* Description:Allocate a virtual block, spanning a given set of physical blocks.
 * 描述：分配一个虚拟块，可能跨越一组物理块
 * 输入：addrs: 一组chunk地址数组Set of block-addresses forming the virtual block
 *      naddrs: 地址个数 The number of addresses in the address-set    
 * 返回：
 */
struct nvm_vblk* nvm_vblk_alloc(struct nvm_dev * dev, struct nvm_addr addrs[], int naddrs)
struct nvm_vblk *vblk = nvm_vblk_alloc(dev, chunk_addrs, naddrs)

/* Description:Retrieve the size, in bytes, of a given virtual block.
 * 描述：检索虚拟块的大小
 * 输入：vblk    
 * 返回：字节数
 */
size_t nbytes = nvm_vblk_get_nbytes(vblk);

/* Description:Write to a virtual block.
 * 描述：将数据写入vblk
 * 输入：vblk: The virtual block to write to   
 *      buf: Write content starting at buf
 *      count: The number of bytes to write 
 * 输出：
 * 返回：字节数 -1
 */
ssize_t nvm_vblk_write(struct nvm_vblk * vblk, const void * buf, size_t count)
nvm_vblk_write(vblk, bufs->write, nbytes)

//内部调用bufs->write = nvm_buf_alloc(dev, bufs->nbytes, NULL)；和read
//和bufs->write_meta = nvm_buf_alloc(dev, bufs->nbytes_meta, NULL);和read
//第3个参数是0，所以不调用第二个函数，如果第2个参数是0，不调用第一个，返回buf
//主要用于测试
struct nvm_buf_set *bufs = nvm_buf_set_alloc(dev, nbytes, 0);


//调用nvm_buf_fill(bufs->write, bufs->nbytes)写入随机数据
//调用memset(bufs->read, 0, bufs->nbytes);将read部分设置为0
//主要用于测试
nvm_buf_set_fill(bufs);

//比较读写内容是否相同
size_t buf_diff = nvm_buf_diff(bufs->read, bufs->write, bufs->nbytes);

/* Description:Find an arbitrary set of ‘naddrs’ block-addresses on the given ‘dev’, 
 * in the given block state ‘bs’ and store them in the provided ‘addrs’ array.
 * 描述：在给定块状态bs中找到一组块地址，并将他们存储到addrs[]中
 * 输入：    
 * 输出：addrs[]
 * 返回：0 -1
 */
int nvm_cmd_gbbt_arbs(struct nvm_dev * dev, int bs, int naddrs, struct nvm_addr addrs[])
nvm_cmd_gbbt_arbs(dev, NVM_BBT_FREE, 1, &blk_addr)

/* Description:
 * 描述：
 * 输入：    
 * 输出：
 * 返回：
 */
/* Description:
 * 描述：
 * 输入：    
 * 输出：
 * 返回：
 */
/* Description:
 * 描述：
 * 输入：    
 * 输出：
 * 返回：
 */
/* Description:
 * 描述：
 * 输入：    
 * 输出：
 * 返回：
 */