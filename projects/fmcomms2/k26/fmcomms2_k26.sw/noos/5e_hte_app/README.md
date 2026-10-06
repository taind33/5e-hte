> **CẬP NHẬT 2026-10-01:** README này viết ngày 08/09 và đã LỖI THỜI ở các chỗ ghi "AD9361 RF sampling/FFT là Phase 2" và "ATT chỉ dB nguyên". Phổ RF (stream_framer.c, WAVE + RFST), ATT bước 0,25 dB, RX ring buffer và lịch XTime đã có - xem `verify_sync/DONG_BO_FW_APP_2026-10-01.md`.

# 5E-HTE Firmware - Phase 1 (AC/DC pwr + F1956 ATT + Optical Delay + ADC7928 + protocol)

Bàn giao này là **Phase 1** như đã thống nhất: nguồn AC/DC, suy hao F1956,
delay quang, đọc ADC7928, và chuẩn giao thức gói tin (framing + CRC).
**AD9361 RF sampling/FFT/nguong phat hien VAN la Phase 2** - xem mục "Phase
2" ở cuối file. AD9361 giờ ĐÃ được khởi tạo (SPI/GPIO/FIR/sampling rate,
xem `ad9361_setup.c`) để sẵn sàng cho Phase 2, nhưng lấy mẫu/FFT/xử lý tín
hiệu vẫn chưa được nối vào.

> **CẬP NHẬT 2026-09-08 (đợt 3)**: nội dung dưới đây mô tả bản Phase 1 gốc
> (thả `src/` phẳng vào 1 project trống, không có AD9361). Project của bạn
> giờ có `main()` MỚI tự viết (không dùng `ad9361/src/main.c` cũ - bạn nói
> file đó chỉ để tham khảo) - và từ đợt 3, AD9361 bring-up nằm THẲNG trong
> `main.c` (không tách `ad9361_setup.c` riêng nữa). Đọc
> **`INTEGRATION_NOTES.md`** ở thư mục gốc để biết chi tiết đầy đủ (cấu trúc
> `src/` mới, một lỗi thật về đường dẫn `app_config.h` đã tìm ra và sửa,
> lưu ý chia sẻ bus SPI giữa AD9361 và F1956, và bằng chứng `noos/` vẫn còn
> nguyên vẹn). Mục "Cấu trúc thư mục" bên dưới vẫn đúng cho protocol/driver
> layer, chỉ khác `main.cpp` nay là `main.c` (gồm cả AD9361 bring-up) +
> `board_app.c` (đã đổi từ .cpp sang .c thuần), và không còn `lscript.ld`
> đi kèm (dùng bản Vitis tự sinh cho project thật của bạn).

## 1. Cấu trúc thư mục

```
src/                    -> TOÀN BỘ code build thật (xem INTEGRATION_NOTES.md)
  main.c                 -> entry point: AD9361 bring-up (viết thẳng ở đây) -> BoardApp_Init() -> vòng lặp BoardApp_PollOnce()
  ad9361_phy.h           -> extern g_ad9361_phy - hook cho Phase 2, không có logic
  parameters.h           -> map XPAR_* rút gọn cho board này (app_config.h THẬT nằm ở ad9361/src/, xem INTEGRATION_NOTES.md mục 1)
  board_config.h        -> map toàn bộ XPAR_* + các hằng số/giả định cần xác nhận (TODO)
  kria_helper.h          -> lớp bản tin trong cùng (kria_message_t) - PORT Y HỆT App/backend/kria_helper.h
  vip_helper.h/.c        -> lớp bọc ngoài VIP (sync+CRC16 header, CRC32 payload) - PORT Y HỆT App/backend/vip_helper.h/.cpp
  circularBuffer.h        -> giữ nguyên từ src_att_pwr_odly (không đổi)
  device_power.h/.c       -> điều khiển nguồn AC (IT-M77xx) / DC (Matsusada-style) qua RS232 - hành vi giữ nguyên 100% so với bản đã chạy
  device_attenuator.h/.c -> driver 3x F1956 (SPI PSU_SPI_0 + LE1/2/3) - giữ nguyên hành vi, làm rõ timing theo datasheet
  device_optical_delay.h/.c -> driver GPIO 2-bit delay quang - giữ nguyên hành vi
  device_adc7928.h/.c    -> driver MỚI cho ADC7928 (SPI fabric XPAR_SPI_0)
  cmd_dispatcher.h/.c    -> parser nhị phân VIP+kria, dispatch lệnh + gửi ACK/telemetry (THAY THẾ Parser_ExecutePacket ASCII cũ)
  board_app.h/.c          -> app điều khiển board (AC/DC/ATT/Delay/ADC7928), gọi bởi main.c

ad9361/                 -> chỉ để THAM KHẢO, KHÔNG nằm trong build (xem INTEGRATION_NOTES.md mục 3)
noos/                   -> thư viện driver AD9361/no-OS thật, không đổi

tools/
  vip_test_client.py     -> tool Python độc lập để test firmware qua UART mà KHÔNG cần app Qt (xem mục 5)
```

Copy toàn bộ `src/` vào Application Project trên Vitis (giữ cấu trúc phẳng
như src_att_pwr_odly gốc). Build Vivado (2022.2) + xuất XSA + tạo Platform +
Application Project trong Vitis như quy trình hiện tại của bạn.

## 2. Vì sao lại thiết kế lại giao thức (đọc trước khi build)

Trong tài liệu bạn gửi có **3 phiên bản codec khác nhau, không tương thích**:

- `App/backend/kria_helper.h`: lớp bản tin App **đang thực sự dùng**
  (`dashboardmanager.cpp` gọi trực tiếp `kria_msg_output_encode`/`kria_decode`,
  KHÔNG có CRC).
- `App/backend/vip_helper.h/.cpp`: lớp bọc CRC đã viết sẵn nhưng **chưa được
  nối vào** `DashboardManager::sendCommand()`/`parseIncomingData()`.
- `codec.zip` (`vip_core.c`/`calculate_crc.c`): một thiết kế VIP **khác nữa**
  (thêm BIND/UNBIND), thiếu cả 2 file header nên không tự đứng được.
- Firmware tham khảo (`src_att_pwr_odly/main.cpp`) lại parse **ASCII text**
  (`"AC,220,50,1\n"`) qua `strtok` - không khớp với bất kỳ codec nhị phân nào
  ở trên.

Theo yêu cầu của bạn ("bản tin bạn sẽ tự defined chuẩn nhúng") và câu trả
lời "giữ nguyên msgid/struct hiện có, chỉ bọc thêm CRC ngoài", **chuẩn cuối
cùng = kria_helper.h (giữ nguyên 100%) + vip_helper (giữ nguyên 100% so với
bản App đã viết sẵn)**. Nghĩa là:

- `firmware/src/kria_helper.h` và `firmware/src/vip_helper.h/.c` là **port
  byte-for-byte** của `App/backend/kria_helper.h` và
  `App/backend/vip_helper.h/.cpp` sang C thuần (không phụ thuộc Qt/BSP).
- **Đã kiểm chứng bằng cách biên dịch cả 2 phía** (C++ phía Qt với header Qt
  bị stub, và C phía firmware) rồi so sánh **từng byte** của cùng 1 gói tin
  mẫu (AC, ATT) - kết quả **giống hệt nhau tuyệt đối**. Đồng thời viết thêm
  `tools/vip_test_client.py` (Python) và xác nhận nó **cũng cho ra byte
  giống hệt** 2 bên kia. Ba cách triển khai độc lập (Qt/C++, firmware/C,
  Python) cho cùng 1 kết quả - đây là bằng chứng tốt nhất có thể có được
  (trong điều kiện không có phần cứng thật để bắt tay) rằng khung giao thức
  đúng và 2 đầu sẽ nói chuyện được với nhau.

### Gói tin trên dây (little-endian)

```
offset  size  field
0       2     SYNC            = 0xEB90
2       1     frameType       10=DATA_STD, 11=PING_STD, 12=ACK_STD
3       1     headerSize      = 9 (cố định)
4       1     control         bit0 = PING_REQUEST
5       2     frameSeq
7       2     dataLen         0 với PING_STD/ACK_STD
9       1     desId
10      1     srcId
11      2     headerCRC16     CRC16-CCITT-FALSE (poly 0x1021, init 0xFFFF) trên 9 byte header
13      N     kria payload    seq(1) + msgid(1) + len(1) + payload[len]  (N = dataLen)
13+N    4     CRC32           CRC32-IEEE802.3 (poly 0xEDB88320, init/final 0xFFFFFFFF) trên N byte trên
```

`desId`/`srcId`: firmware = `0x01` (`VIP_NODE_ID_SELF`), Qt host = `0x00`
(`VIP_NODE_ID_HOST`) - khớp đúng hằng số đã có sẵn trong
`App/backend/vip_helper.h`.

### msgid (không đổi so với App hiện có, + 1 msgid mới)

| msgid | struct            | chiều              | ý nghĩa |
|-------|--------------------|--------------------|---------|
| 35    | `fwi_msg_t`        | host->fw, fw->host | AC power set / ACK (payload64[0]=1 thành công) |
| 36    | `fwi_msg_t`        | host->fw, fw->host | DC power set / ACK |
| 37    | `timedelay_msg_t`  | host->fw, fw->host | Optical delay set / ACK |
| 38    | `annuation_msg_t`  | host->fw, fw->host | Suy hao set / ACK |
| 39    | `cmd_msg_t`        | host->fw           | Lệnh generic/diag (CMD: box trên app) |
| **40**| **`adc7928_msg_t`**| **fw->host**       | **MỚI: telemetry 8 kênh ADC7928** |

## 3. ADC7928 - driver mới (theo đúng datasheet, không đoán)

Đã tra cứu datasheet chính thức:
- F1956 (Renesas/IDT): https://www.mouser.com/datasheet/2/698/REN_F1956_DST_20230421-2930778.pdf
- AD7908/AD7918/AD7928 (Analog Devices): https://www.analog.com/media/en/technical-documentation/data-sheets/AD7908_7918_7928.pdf

Theo bạn xác nhận, ADC7928 đấu vào **SPI fabric XPAR_SPI_0** (AXI Quad SPI,
base `0x80000000`, đã có sẵn trong xparameters.h nhưng chưa nguồn tham khảo
nào dùng tới - khớp với việc đây là phần cứng mới). Driver dùng `XSpi`
(driver "PL" của Xilinx, khác `XSpiPs` dùng cho F1956).

Thông số áp dụng đúng datasheet AD7928:
- CS active low, DIN/DOUT clock trên cạnh xuống SCLK => **CPOL=0, CPHA=1**.
- 16 SCLK/chu kỳ chuyển đổi; 12 bit đầu là thanh ghi điều khiển
  (WRITE=1, ADD2-0=kênh, PM1:0=11 bình thường, RANGE, CODING=1 nhị phân).
- **Độ trễ 1 chu kỳ**: kết quả trả về trong 1 lần transfer luôn thuộc về
  kênh đã chọn ở lần transfer **trước đó**, không phải kênh đang chọn ngay
  lúc đó. `ADC7928_ReadAll()` xử lý đúng việc này (9 lần transfer cho 8
  kênh, có kiểm tra 3-bit địa chỉ trả về để tự phát hiện lỗi bus).

**TODO(cần bạn xác nhận trước khi tin số liệu)**: driver gửi lên **mã ADC
thô (0..4095)**, chưa quy đổi ra vôn thật, vì **không có sơ đồ mạch analog
front-end** (tỉ lệ chia áp) cho kênh đo "điện áp kích nổ" - giá trị này gần
chắc chắn lên tới hàng chục/hàng trăm volt, vượt xa dải vào của ADC. Xem
`board_config.h` (`ADC7928_VREF_VOLTS`, `ADC7928_CH_DIVIDER_RATIO`) và
`ADC7928_CodeToVolts()` trong `device_adc7928.c` - chỉ cần sửa 2 hằng số đó
khi có tỉ lệ chia áp thật, không cần build lại logic SPI.

## 4. Phát hiện quan trọng khác (đã tự sửa, không chỉ báo suông)

1. **`XPAR_SCUGIC_SINGLE_DEVICE_ID` không tồn tại** trong xparameters.h bạn
   gửi (macro đó dành cho BSP Zynq-7000/PS7 cũ) - `src_att_pwr_odly/main.cpp`
   dùng tên này nên **sẽ không build được** với xparameters.h hiện tại. Đã
   sửa thành `XPAR_SCUGIC_0_DEVICE_ID` (tên thật được sinh ra cho board
   K26I/ZynqMPSoC của bạn).

2. **Không có UART nào (RS232_AC_PWR/RS232_DC_PWR/RS422_PC) được nối ngắt
   tới GIC** trong xparameters.h bạn gửi - chỉ 2 ngắt DMA của AD9361
   (`XPAR_FABRIC_AXI_AD9361_{DAC,ADC}_DMA_IRQ_INTR`) là có nối. Firmware
   được viết để **tự thích ứng**:
   - AC/DC UART: firmware chỉ **gửi** lệnh, không bao giờ đọc phản hồi, nên
     được khởi tạo hoàn toàn không cần ngắt (giản lược so với bản tham khảo).
   - RS422/PC: nếu `XPAR_FABRIC_RS422_PC_IP2INTC_IRPT_INTR` được định nghĩa
     (ví dụ sau khi bạn nối lại ngắt này trong Vivado), firmware tự dùng
     kiến trúc ngắt + circular buffer y hệt bản tham khảo; nếu không, tự
     động chuyển sang **polling** trực tiếp trong vòng lặp chính. Đã build
     + link thử **cả 2 kịch bản** (xem mục 6) - cả 2 đều sạch.
   - **Khuyến nghị**: polling vẫn nhận đúng được toàn bộ khung ở 115200 baud
     trong điều kiện tải CPU thấp như thiết kế hiện tại (vòng lặp chính
     không làm gì nặng ngoài đọc ADC7928 mỗi ~1s), nhưng **nối ngắt RS422/PC
     tới GIC trong Vivado vẫn là lựa chọn chắc chắn hơn về lâu dài** cho một
     giao thức có CRC nhị phân - bạn cân nhắc khi có dịp sửa block design.

## 5. Test không cần app Qt (`tools/vip_test_client.py`)

```bash
pip install pyserial
python3 tools/vip_test_client.py --port /dev/ttyUSB0 --baud 115200 ac 220 50 1
python3 tools/vip_test_client.py --port /dev/ttyUSB0 dc 24 1 1
python3 tools/vip_test_client.py --port /dev/ttyUSB0 att 2 15
python3 tools/vip_test_client.py --port /dev/ttyUSB0 delay 270
python3 tools/vip_test_client.py --port /dev/ttyUSB0 listen     # xem telemetry ADC7928 msgid 40 tự động mỗi ~1s
```

Tool này encode/decode đúng y hệt thuật toán CRC16/CRC32 và layout gói tin
của firmware (đã đối chiếu byte-for-byte - mục 6), nên có thể dùng ngay để
bring-up phần cứng trước khi App (Phase 2) được cập nhật để tự bọc VIP.

## 6. Đã kiểm chứng những gì (trước khi đóng gói)

Không có Vitis/Vivado trong môi trường này nên **không build được bằng
toolchain ARM thật**, nhưng đã làm nghiêm túc nhất có thể trong điều kiện đó:

- **Đối chiếu giao thức 3 chiều**: biên dịch `App/backend/vip_helper.cpp`
  (C++/Qt, stub `<QDebug>`), `src/vip_helper.c` (C, firmware) và
  `tools/vip_test_client.py` (Python) trên cùng bộ dữ liệu mẫu (gói AC,
  gói ATT) - **byte output giống hệt nhau tuyệt đối** cả 3 bên, kể cả khi
  giải mã ngược lại (`VIP_Decode`) trên firmware.
- **Biên dịch + link toàn bộ `src/*.c`/`main.cpp`** bằng gcc/g++ với bộ
  header giả lập bám sát chữ ký API thật của `xspips.h`/`xspi.h`/
  `xgpio.h`/`xuartns550.h`/`xscugic.h`/`xil_exception.h` (chỉ để bắt lỗi cú
  pháp/kiểu dữ liệu/thiếu khai báo - KHÔNG thay thế cho build Vitis thật).
  Việc này đã **bắt được 2 lỗi thật** trước khi giao (mục 4.1 và việc dùng
  sai macro ngắt UART) thay vì chỉ đoán.
- Build sạch (0 cảnh báo) ở cấu hình khớp xparameters.h thật của bạn (không
  có ngắt RS422/PC); build sạch (chỉ 2 cảnh báo cast con trỏ hàm vô hại,
  cùng kiểu cast đã có trong code tham khảo gốc) ở cấu hình giả lập có ngắt.
- **Việc bạn cần làm khi có Vitis thật**: build lại trong IDE thật để bắt
  các lỗi chỉ toolchain ARM GCC + BSP thật mới thấy được (ví dụ tên field
  lệch phiên bản BSP) - các stub header ở đây cố ý bám sát API nhưng không
  thể thay thế 100% cho BSP thật.

## 7. Giới hạn đã biết / cần theo dõi ở Phase 2 (sửa App)

- `fwi_msg_t.frequency` bị App dùng lại (tái sử dụng) để chứa **dòng DC**
  (xem `DashboardManager::setDcPower()`), ép kiểu về `uint16_t` => **mất
  phần thập phân của dòng điện** (vd 1.5A -> 1A). Firmware Phase 1 chỉ
  dùng đúng giá trị App gửi (không tự "sửa" hộ vì sẽ phá tương thích) -
  nên sửa ở App bằng cách nhân 10/100 trước khi ép kiểu, kèm sửa firmware
  chia lại tương ứng.
- `annuation_msg_t.value` là số dB nguyên (0..31), mất độ phân giải 0.25dB
  thật của F1956 - do giới hạn của struct hiện có, không phải bug firmware.
- UI `attenuation.qml` hiện cho nhập 0-63, cần sửa còn 0-31 (đã xác nhận
  0-31.75dB/bước 0.25dB là đúng theo bạn).
- `App/backend/vip_helper.cpp` chưa được gọi từ
  `DashboardManager::sendCommand()`/`setAcPower()`/... /`parseIncomingData()`
  - đây chính là phần việc chính của Phase 2 (chỉ cần bọc/mở VIP quanh
  logic kria đã có, không cần viết lại).

## 8. Phase 2 (chưa làm trong lượt này)

- AD9361: init (dựa trên `ref_ad9361_init`/no-OS driver trong `noos.zip`),
  lấy mẫu DMA, FFT, tính dBm, ngưỡng "Detected". `ref_ad9361_init_1`
  (fmcw_5e15.c/h) tham chiếu tới các IP tùy biến
  (`phase_inc_ctrl`, `adrf5700_ctrl`, `adf4360_ctrl`, `action_fire_ctrl`...)
  **không tồn tại trong xparameters.h của dự án này** - nhiều khả năng đây
  là tài liệu từ 1 dự án FMCW radar khác, sẽ xác nhận lại với bạn trước khi
  dùng bất kỳ phần nào của nó.
- App/Qt: nối `vip_helper.cpp` vào `DashboardManager`, sửa giới hạn ATT,
  sửa mã hoá dòng DC, dùng huashu-design để đánh giá/chỉnh thẩm mỹ + bảng
  màu + cách scale cho Qt 6.11.2.
