/*H**********************************************************************
* FILENAME :        bootjck.c
*
* DESCRIPTION :
*       Overwrites the boot sector image
*
* NOTES :
*
* AUTHOR :    Lumi Hyväri <lumi.hyvari@gmail.com>       START DATE :    15 Aug 2025
*
*H*/

// Standards
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <stdbool.h>

// Block device interface
#include <unistd.h>
#include <fcntl.h>

// Ntohl
#include <arpa/inet.h>

// Syslogging
#include <sys/syslog.h>
#include <sys/stat.h>

// Core data structures
#include "include/core.h"
#include "include/efi.h"

bool read_only;

void help(void) {
    printf("BOOTJCK | 1.0 | BOOT IMAGE OVERWRITER\n\nSUPPORTS.\n    UEFI    GPT\n\nUSAGE.\n    ./bootjck <block device> <image file>\n\nEXAMPLE.\n    ./bootjck /dev/sda NEW_BOOT.EFI\n\n");
}

struct gpt_header_t * seek_gpt_header(int hDevice, long int addr) {
    long int end;
    GPT_SIGNATURE_TYPE signature_buffer;
    struct gpt_header_t * header = malloc(sizeof(struct gpt_header_t));

    syslog(LOG_INFO, "Seeking to GUID partition table header (from %lx) ...", lseek(hDevice, 0, SEEK_CUR));

    if((end = lseek(hDevice, 0, SEEK_END)) == -1) {
        syslog(LOG_ERR, "Unable to seek. Error: %s", strerror(errno));
    }
    addr = 0;
    while(addr != end) {
        if(lseek(hDevice, addr, SEEK_SET)== -1) {
            syslog(LOG_ERR, "Unable to seek. Error: %s", strerror(errno));
        }
        if(read(hDevice, &signature_buffer, sizeof(GPT_SIGNATURE_TYPE)) == -1) {
            syslog(LOG_ERR, "Unable to read %lx. (Error: %s)", addr, strerror(errno));
        }
        // Rewind to original address, due to read()
        if(lseek(hDevice, -(sizeof(GPT_SIGNATURE_TYPE)), SEEK_CUR)== -1) {
            syslog(LOG_ERR, "Unable to seek. Error: %s", strerror(errno));
        }
        printf("gptheadersearch <%lx> %-20lx == %20lx ... ", addr, signature_buffer, GPT_HEADER_SIGNATURE);
        // Check if signature was found on read
        if(signature_buffer == GPT_HEADER_SIGNATURE) {
            printf("MATCH!\n");
            syslog(LOG_INFO, "Found the GUID partition table signature %lx at %lx", signature_buffer, addr);
            // Signature was found, breaking.
            break;
        }else{ printf("\n"); }
        addr++;
    }
    if(read(hDevice, header, sizeof(struct gpt_header_t)) != -1) {
        // If found and read, then return header ptr
        return header;
    }
    // If not found, free header structure and return NULL
    free(header);
    syslog(LOG_ERR, "Unable to read %lx. (Error: %s)", addr, strerror(errno));
    return NULL;
}

struct gpt_entry_t * seek_efi_system_partition_entry(int hDevice, struct gpt_header_t * header, long int addr) {
    struct gpt_entry_t * entry = malloc(sizeof(struct gpt_entry_t));
    syslog(LOG_INFO, "Reading and enumerating GUID partition table entries in search for EFI system partition entry (from %lx) ...", lseek(hDevice, 0, SEEK_CUR));
    if(lseek(hDevice, addr, SEEK_SET) == -1) {
        syslog(LOG_ERR, "Unable to seek to %lx (end of header). Error: %s", addr, strerror(errno));
    } // assuming addr is end of header, which is the start of entry table

    for(int p = 0; p < header->numberofpartitionentries; p++) {
        if(read(hDevice, entry, sizeof(struct gpt_entry_t)) == -1) {
            syslog(LOG_ERR, "Unable to read GUID partition table entry. Error: %s", strerror(errno));
        }
        // Skip reserved field (SizeOfPartitionEntry - 128)
        if(lseek(hDevice, header->sizeofpartitionentry - 128, SEEK_CUR) == -1) {
            syslog(LOG_ERR, "Unable to seek ahead of reserved field (%d bytes). Error: %s", header->sizeofpartitionentry - 128, strerror(errno));
        }

        // Print verbose information
        printf("efipartsearch <%lx> %-20lx == %20lx ... ", lseek(hDevice, 0, SEEK_CUR), (unsigned long)entry->partitiontypeguid, (unsigned long)*EFI_PARTITION_SIGNATURE);

        // Control if GUID is EFI partition
        if(entry->partitiontypeguid == *EFI_PARTITION_SIGNATURE) {
            // If so, then return entry ptr
            syslog(LOG_INFO, "Found the EFI system partition entry at %lx", lseek(hDevice, 0, SEEK_CUR));
            printf("MATCH!\n");
            return entry;
        }
        printf("\n");
    }
    free(entry);
    return NULL;
}

int main(int argc, char * args[]) {
    // Syslog initiation
    openlog ("BOOTJCK", LOG_INFO | LOG_PID | LOG_NDELAY, LOG_LOCAL1);
    syslog(LOG_MAKEPRI(LOG_LOCAL1, LOG_NOTICE), "Program started by User %d", getuid ());

    // Arguments control
    if(argc < 3) {
        help();
        return 0;
    }
    for(int x = 0; x < argc; x++) {
        if(strcmp(args[x], "--read-only") == 0) {
            syslog(LOG_INFO, "* * * Read Only activated by parameter!");
            read_only = true;
        }
    }
    printf("\a\n");

    // Variables
    char DevicePath[strlen(args[1])];
    char ImagePath[strlen(args[2])];
    int hDevice;
    struct stat stat_buffer;
    struct gpt_header_t * header;
    struct gpt_entry_t * entry;

    // Get the file paths
    strcpy(DevicePath, args[1]);
    strcpy(ImagePath, args[2]);

    // Check if both files exist
    syslog (LOG_INFO, "Checking if image %s exists ... ", ImagePath);
    if(stat(ImagePath, &stat_buffer) != 0) {
        syslog(LOG_ERR, "Error: %s", strerror(errno));
        return 1;
    }

    syslog (LOG_INFO, "Checking if block device %s exists ... ", DevicePath);
    if(stat(DevicePath, &stat_buffer) != 0) {
        syslog(LOG_ERR, "Error: %s", strerror(errno));
        return 1;
    }

    syslog(LOG_INFO, "OwO Let's start messing shit up");

    // Open block device
    syslog(LOG_INFO, "Opening device %s", DevicePath);
    if((hDevice = open(DevicePath, read_only ? O_RDONLY : O_RDWR)) == -1)
        {
            syslog(LOG_ERR, "Error: %s", strerror(errno));
            return 1;
        }

    // Find the GUID partition table header
    header = seek_gpt_header(hDevice, lseek(hDevice, 0, SEEK_SET)); // SEEK_CUR == end of header if success

    // Enumerate GUID partition table entries, seeking for EFI system partition entry
    if((entry = seek_efi_system_partition_entry(hDevice, header, lseek(hDevice, 0, SEEK_CUR))) == NULL) {
        syslog(LOG_ERR, "Shit did not work");
    }

    // Close down handles
    close(hDevice);
    closelog();
    return 0;
}
