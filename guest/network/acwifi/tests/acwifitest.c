/* Tiny acwifi.device smoke/probe. MIT. */
#include <exec/types.h>
#include <exec/io.h>
#include <proto/exec.h>
#include <stdio.h>
#include <string.h>

#include <acwifi_device.h>

static LONG acw_call(struct Device *dev, struct ACWiFiCall *call)
{
    register struct Device *a6 __asm("a6") = dev;
    register struct ACWiFiCall *a0 __asm("a0") = call;
    register LONG d0 __asm("d0");
    __asm volatile ("jsr -42(%%a6)" : "=r"(d0), "+r"(a0) : "r"(a6)
                    : "d1", "a1", "cc", "memory");
    return d0;
}

int main(void)
{
    struct MsgPort *port;
    struct IOStdReq *io;
    struct ACWiFiCall call;
    struct ACWiFiNetwork row;
    LONG rc = 0;

    port = CreateMsgPort();
    if (!port) return 20;
    io = (struct IOStdReq *)CreateIORequest(port, sizeof(*io));
    if (!io) { DeleteMsgPort(port); return 20; }
    if (OpenDevice(ACWIFI_DEVICE_NAME, 0, (struct IORequest *)io, 0)) {
        puts("gfopensocketwifi.device: OPEN FAIL");
        DeleteIORequest((struct IORequest *)io);
        DeleteMsgPort(port);
        return 20;
    }

    memset(&call, 0, sizeof(call));
    memset(&row, 0, sizeof(row));
    call.command = ACWIFI_STATUS;
    call.networks = &row;
    call.capacity = 1;
    acw_call(io->io_Device, &call);
    printf("gfopensocketwifi.device open: PASS\nstatus result=%ld errno=%lu\n",
           (long)call.result, (unsigned long)call.error);

    CloseDevice((struct IORequest *)io);
    DeleteIORequest((struct IORequest *)io);
    DeleteMsgPort(port);
    return rc;
}
