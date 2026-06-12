# LwIP Configuration Analysis

## Files compared

| File | Purpose |
|---|---|
| `LWIP/Target/lwipopts_vector.h` | Active STM32H753 configuration (STM32CubeMX, FreeRTOS/CMSIS port) |
| `LWIP/Target/lwipopts_jlr.h` | NXP MCUXpresso code-generation **template** (not directly buildable) |
| `LWIP/Target/coniguration_summary.h` | Resolved JLR values (template substituted) |

---

## Key differences: vector vs JLR (resolved)

| Setting | lwipopts_vector.h | JLR actual | Note |
|---|---|---|---|
| `LWIP_SOCKET` | not explicit (defaults to 1) | `1` | Same effective value |
| `LWIP_NETCONN` | not explicit (defaults to 1) | `1` | Same effective value |
| `LWIP_IPV6` | not defined (disabled) | `1` | Not used in this project |
| `MEM_SIZE` | `16385` | `65535` | JLR heap ~4× larger |
| `PBUF_POOL_SIZE` | not defined | `0` | JLR disables pbuf pool |
| `MEMP_NUM_PBUF` | not defined | `8` | |
| `MEMP_NUM_UDP_PCB` | not defined | `8` | |
| `MEMP_NUM_SYS_TIMEOUT` | not defined | `15` | |
| `TCPIP_THREAD_STACKSIZE` | `4096` | `0` (unfilled template) | vector value correct |
| `DEFAULT_THREAD_STACKSIZE` | `2048` | `0` (unfilled template) | vector value correct |
| `TCPIP_MBOX_SIZE` | `6` | `20` | See note below |
| `DEFAULT_UDP_RECVMBOX_SIZE` | `6` | `10` | See note below |
| `LWIP_STATS` | `0` | `1` | vector disables for production |
| `CHECKSUM_*` | all `0` (HW offload) | SW checksums | Transparent to application |
| `LWIP_NETIF_LINK_CALLBACK` | `1` | `0` | vector has link callbacks |
| `LWIP_NETIF_STATUS_CALLBACK` | `1` | `0` | vector has status callbacks |
| `LWIP_TCPIP_CORE_LOCKING` | not defined (0) | `1` | Different locking model |
| `LWIP_RAM_HEAP_POINTER` | `0x30004C00` | not defined | STM32 SRAM_D2 specific |

---

## Compatibility assessment

**Project uses:** UDP only (no TCP), socket API.

**Conclusion: No major compatibility concerns.**

- TCP differences are irrelevant (TCP not used).
- IPv6 not needed; omitting it saves RAM.
- `LWIP_SOCKET` is enabled by default when `NO_SYS=0`; no explicit define required (but can be added for clarity).
- Hardware checksum offload in vector config is transparent to the socket API.
- Thread stack sizes in JLR are `0` (unfilled template defaults) — vector values of `4096`/`2048` are correct and must be kept.

---

## UDP receive path and mailbox sizing

Incoming UDP packets pass through two queues before reaching the application task:

```
NIC → ethernetif → TCPIP_MBOX → tcpip_thread → UDP recvmbox → application task
```

`TCPIP_MBOX_SIZE` is the **tcpip thread input queue** and affects all protocols (UDP, TCP, RAW, API calls). A burst of UDP packets can be dropped here before they ever reach the per-socket queue.

**Current vector values vs JLR:**

| Setting | vector | JLR |
|---|---|---|
| `TCPIP_MBOX_SIZE` | 6 | 20 |
| `DEFAULT_UDP_RECVMBOX_SIZE` | 6 | 10 |

Consider increasing both if UDP packet drops are observed under burst load.

---

## STM32H753-specific settings (vector only)

- `LWIP_RAM_HEAP_POINTER 0x30004C00` — LwIP heap placed in SRAM_D2 after the RX DMA pool (`0x300000c0`–`0x30004A5F`).
- `ETH_RX_BUFFER_SIZE 1536` — aligned to Ethernet MTU.
- `CHECKSUM_BY_HARDWARE 1` — ETH peripheral handles IP/UDP/TCP checksum generation and validation.
- `LWIP_NETCONN_THREAD_SEM_TLS_INDEX 0` — per-thread semaphore stored in FreeRTOS TLS slot 0 (required by `LWIP_NETCONN_SEM_PER_THREAD=1` on CMSIS port).
