#include "ata.h"


ide_channel_regs_t ide_channels[2];
ide_device_t ide_devices[4];
uint8_t ide_buf[2048] = {0};
uint8_t ide_irq_invoked = 0;
uint8_t atapi_packet[12] = {0xA8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

void ide_detect() {
	
}

void ata_io_wait(uint16_t bus) {
	port_byte_in(bus + ATA_REG_ALTSTATUS);
	port_byte_in(bus + ATA_REG_ALTSTATUS);
	port_byte_in(bus + ATA_REG_ALTSTATUS);
	port_byte_in(bus + ATA_REG_ALTSTATUS);
}

int ata_wait(uint16_t bus, int advanced) {
	uint8_t status = 0;

	ata_io_wait(bus);

	while ((status = port_byte_in(bus + ATA_REG_STATUS)) & ATA_SR_BSY);

	if (advanced) {
		status = port_byte_in(bus + ATA_REG_STATUS);
		if (status   & ATA_SR_ERR)  return 1;
		if (status   & ATA_SR_DF)   return 1;
		if (!(status & ATA_SR_DRQ)) return 1;
	}

	return 0;
}

void ata_select(uint16_t bus) {
	port_byte_out(bus + ATA_REG_HDDEVSEL, 0xA0);
}

void ata_wait_ready(uint16_t bus) {
	while (port_byte_in(bus + ATA_REG_STATUS) & ATA_SR_BSY);
}

void ide_init(uint16_t bus) {

	//debug_print(NOTICE, "initializing IDE device on bus %d", bus);

	port_byte_out(bus + 1, 1);
	port_byte_out(bus + 0x306, 0);

	ata_select(bus);
	ata_io_wait(bus);

	port_byte_out(bus + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
	ata_io_wait(bus);

	int status = port_byte_in(bus + ATA_REG_COMMAND);
	//debug_print(INFO, "status = %x", status);

	ata_wait_ready(bus);

	ata_identify_t device;
	uint16_t * buf = (uint16_t *)&device;

	for (int i = 0; i < 256; ++i) {
		buf[i] = port_word_in(bus);
	}

	uint8_t * ptr = (uint8_t *)&device.model;
	for (int i = 0; i < 39; i+=2) {
		uint8_t tmp = ptr[i+1];
		ptr[i+1] = ptr[i];
		ptr[i] = tmp;
	}

	//debug_print(NOTICE, "ata device %s", device.model);
	//debug_print(NOTICE, "sectors_48 = %d", (uint32_t)device.sectors_48);
	//debug_print(NOTICE, "sectors_28 = %d", device.sectors_28);

	port_byte_out(bus + ATA_REG_CONTROL, 0x02);
}

void ide_read_sector(uint16_t bus, uint8_t slave, uint32_t lba, uint8_t * buf) {
	port_byte_out(bus + ATA_REG_CONTROL, 0x02);

	ata_wait_ready(bus);

	port_byte_out(bus + ATA_REG_HDDEVSEL,  0xe0 | slave << 4 | 
								 (lba & 0x0f000000) >> 24);
	port_byte_out(bus + ATA_REG_FEATURES, 0x00);
	port_byte_out(bus + ATA_REG_SECCOUNT0, 1);
	port_byte_out(bus + ATA_REG_LBA0, (lba & 0x000000ff) >>  0);
	port_byte_out(bus + ATA_REG_LBA1, (lba & 0x0000ff00) >>  8);
	port_byte_out(bus + ATA_REG_LBA2, (lba & 0x00ff0000) >> 16);
	port_byte_out(bus + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

	if (ata_wait(bus, 1)) {
		//debug_print(WARNING, "Error during ATA read");
	}

	int size = 256;
	inportsm(bus,buf,size);
	ata_wait(bus, 0);
}

void ide_write_sector(uint16_t bus, uint8_t slave, uint32_t lba, uint8_t * buf) {
	port_byte_out(bus + ATA_REG_CONTROL, 0x02);

	ata_wait_ready(bus);

	port_byte_out(bus + ATA_REG_HDDEVSEL,  0xe0 | slave << 4 | 
								 (lba & 0x0f000000) >> 24);
	ata_wait(bus, 0);
	port_byte_out(bus + ATA_REG_FEATURES, 0x00);
	port_byte_out(bus + ATA_REG_SECCOUNT0, 0x01);
	port_byte_out(bus + ATA_REG_LBA0, (lba & 0x000000ff) >>  0);
	port_byte_out(bus + ATA_REG_LBA1, (lba & 0x0000ff00) >>  8);
	port_byte_out(bus + ATA_REG_LBA2, (lba & 0x00ff0000) >> 16);
	port_byte_out(bus + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);
	ata_wait(bus, 0);
	int size = 256;
	outportsm(bus,buf,size);
	port_byte_out(bus + 0x07, ATA_CMD_CACHE_FLUSH);
	ata_wait(bus, 0);
}

int ide_cmp(uint32_t * ptr1, uint32_t * ptr2, size_t size) {
	assert(!(size % 4));
	uint32_t i = 0;
	while (i < size) {
		if (*ptr1 != *ptr2) return 1;
		ptr1++;
		ptr2++;
		i += 4;
	}
	return 0;
}

void ide_write_sector_retry(uint16_t bus, uint8_t slave, uint32_t lba, uint8_t * buf) {
	uint8_t * read_buf = kmalloc(512);
	IRQ_OFF;
	do {
		ide_write_sector(bus,slave,lba,buf);
		ide_read_sector(bus,slave,lba,read_buf);
	} while (ide_cmp((uint32_t*)buf,(uint32_t*)read_buf,512));
	IRQ_RES;
}
