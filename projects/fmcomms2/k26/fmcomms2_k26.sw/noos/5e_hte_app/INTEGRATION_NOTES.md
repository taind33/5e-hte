> **CẬP NHẬT 2026-10-01:** README này viết ngày 08/09 và đã LỖI THỜI ở các chỗ ghi "AD9361 RF sampling/FFT là Phase 2" và "ATT chỉ dB nguyên". Phổ RF (stream_framer.c, WAVE + RFST), ATT bước 0,25 dB, RX ring buffer và lịch XTime đã có - xem `verify_sync/DONG_BO_FW_APP_2026-10-01.md`.

# Ghi chú tích hợp - đợt 3 (2026-09-08): merge AD9361 vào main.c + fix lỗi app_config.h

Bạn hỏi vì sao có vẻ như `noos/` bị bỏ, và muốn: (1) build sạch các tính
năng khác (AC/DC/ATT/Delay/ADC7928) trước, (2) AD9361 init tham khảo
`ad9361/` nhưng đặt code init NGAY TRONG `main.c` (không tách module riêng
nữa). Cả hai đã làm xong bên dưới. Trong lúc làm, tôi phát hiện và sửa một
lỗi thật (mục 2) chứ không chỉ là hiểu lầm.

## 0. Trả lời thẳng: `noos/` có bị bỏ không?

**Không.** `noos/` vẫn còn nguyên 856KB, đầy đủ mọi file (`ad9361.c`,
`ad9361_api.c` ~237KB+73KB, `axi_adc_core.c`, `axi_dac_core.c`,
`axi_dmac.c`, toàn bộ platform layer Xilinx) - không file nào bị xoá so với
zip gốc bạn gửi. `main.c` vẫn gọi thẳng `ad9361_init()` (hàm thật trong
`ad9361_api.c`), hàm này tự gọi `axi_adc_init()` cho lõi RX bên trong nó
(xem `ad9361_api.c` dòng ~568: `axi_adc_init(&phy->rx_adc,
init_param->rx_adc_init)`), đúng như code tham khảo của ADI vẫn làm - đây
không phải chỗ tôi viết ra, tôi chỉ gọi vào.

Điều duy nhất KHÔNG có trong bản build là `axi_dmac_init()`/DMA transfer
(đọc/ghi mẫu RF qua DMA) - nhưng bản thân `ad9361/src/main.c` gốc của bạn
CŨNG không bật đường này theo mặc định: các lời gọi `axi_dmac_init()` ở đó
bị comment sẵn (`//	status = axi_dmac_init(...)`), và toàn bộ code DMA
transfer nằm sau `#ifdef DMA_EXAMPLE` - một macro không được định nghĩa ở
đâu cả trong project gốc. Tức là: file tham khảo của bạn, khi build đúng
như nó vốn có, cũng chỉ bring-up chip (không có data-path RF sống) - giống
hệt những gì `main.c` mới làm bây giờ. DMA/RF sampling thật sự là Phase 2
(xem README.md), lúc đó mới cần bật `axi_dmac_init()`.

## 1. Lỗi thật đã tìm ra: `app_config.h` không nằm ở chỗ vendor code tìm

Đây là phát hiện quan trọng nhất đợt này. 3 file trong driver no-OS
(vendor, không được sửa) include `app_config.h` bằng đường dẫn TƯƠNG ĐỐI
CỐ ĐỊNH, không phụ thuộc việc file nào bị "Exclude from Build":

```
noos/drivers/rf-transceiver/ad9361/ad9361.c:
    #include "../../../../ad9361/src/app_config.h"
noos/drivers/rf-transceiver/ad9361/ad9361_api.c:
    #include "../../../../ad9361/src/app_config.h"
noos/drivers/rf-transceiver/ad9361/ad9361_util.h:
    #include "../../../../ad9361/src/app_config.h"
```

4 dấu `../` từ `noos/drivers/rf-transceiver/ad9361/` đi lên gốc project rồi
xuống `ad9361/src/app_config.h` - LUÔN LUÔN là file đó, bất kể
`ad9361/src/main.c` có bị loại khỏi build hay không. Ở đợt 2, tôi tạo một
bản `app_config.h` MỚI, đã sửa đúng (định nghĩa `XILINX_PLATFORM`) nhưng đặt
ở `src/app_config.h` - tưởng rằng cả thư mục `ad9361/` đã "chết", không ai
đọc tới nữa. **Sai**: 3 file vendor ở trên chưa bao giờ nhìn thấy bản
`src/app_config.h` đó - chúng vẫn đọc bản CŨ tại `ad9361/src/app_config.h`,
bản này để `XILINX_PLATFORM` bị comment (`//#define XILINX_PLATFORM`),
dựa vào một preprocessor symbol khai báo ở cấp Eclipse project mà project
gốc bạn gửi (không có `.cproject`) không xác nhận được có tồn tại hay
không. Nếu không có, driver AD9361 thật sẽ thiếu các nhánh code
`#ifdef XILINX_PLATFORM` khi build thật trong Vitis.

**Đã sửa**: sửa thẳng vào file tại đúng chỗ vendor code cần
(`ad9361/src/app_config.h`), định nghĩa `XILINX_PLATFORM` trực tiếp ở đó
(không dựa vào symbol ẩn). Xoá bản trùng lặp vô dụng ở `src/app_config.h`.
Từ giờ chỉ còn ĐÚNG MỘT file `app_config.h` trong toàn project.

Hệ quả: mặc dù `main()` cũ trong `ad9361/src/main.c` chỉ là tham khảo,
**thư mục `ad9361/src/` không được coi là "chết hoàn toàn"** -
`app_config.h` ở đó vẫn phải tồn tại và đúng nội dung, và thư mục đó vẫn
phải nằm trong include path của project (xem mục 4) - dù chính
`ad9361/src/main.c` thì vẫn bị loại khỏi build như bạn yêu cầu.

Đã kiểm chứng bằng cách resolve đường dẫn thật (`realpath` từ đúng vị trí
`ad9361.c`) và bằng compile thật (mục 5) - không phải suy đoán.

## 2. AD9361 bring-up giờ nằm thẳng trong `main.c`

Theo đúng yêu cầu "init trong main chính": xoá `ad9361_setup.c`/`.h`, gộp
toàn bộ nội dung (struct config SPI/GPIO, FIR RX/TX, struct
`AD9361_InitParam` ~200 trường copy y hệt bản gốc, và hàm bring-up - nay đổi
tên `Ad9361_Bringup()`, `static` trong `main.c`) trực tiếp vào `main.c`,
giống hệt cách `ad9361/src/main.c` gốc của bạn làm (code cấu hình và
`main()` cùng một file). `main()` chỉ còn:

```c
int main(void)
{
    if (Ad9361_Bringup() != XST_SUCCESS) { ... halt ... }
    BoardApp_Init();
    while (1) { BoardApp_PollOnce(); }
}
```

Handle `ad9361_phy` sau khi khởi tạo được lưu vào biến toàn cục
`g_ad9361_phy`, khai báo `extern` trong 1 header rất nhỏ - `ad9361_phy.h` -
để dành cho việc nối AD9361 runtime (đổi tần số/gain khi đang chạy) vào
dispatcher ở Phase 2 sau này, mà không cần kéo cả `main.c` vào nơi khác.

## 3. `src/` giờ gồm những gì

```
src/                    -> TOÀN BỘ code build thật của dự án
  main.c                -> entry point DUY NHẤT: AD9361 bring-up (viết thẳng ở đây) -> BoardApp_Init() -> vòng lặp BoardApp_PollOnce()
  ad9361_phy.h          -> khai báo extern g_ad9361_phy - hook cho Phase 2, không có logic
  parameters.h          -> map XPAR_* rút gọn, chỉ còn những gì main.c thực sự dùng
  board_app.h/.c        -> app điều khiển board (AC/DC/ATT/Delay/ADC7928 qua RS422/PC, giao thức VIP+kria)
  board_config.h        -> map XPAR_* cho thiết bị board-control + hằng số timing/analog cần xác nhận (TODO)
  kria_helper.h, vip_helper.h/.c, circularBuffer.h -> lớp giao thức
  device_power.h/.c, device_attenuator.h/.c, device_optical_delay.h/.c, device_adc7928.h/.c -> driver thiết bị
  cmd_dispatcher.h/.c   -> dispatch lệnh nhị phân

ad9361/src/             -> GIỮ NGUYÊN main.c/parameters.h/fmcw_5e15.* làm THAM KHẢO (không add vào build)
                           NHƯNG app_config.h ở đây là file THẬT, đang được dùng (xem mục 1) - đừng xoá/đổi tên nó
noos/                   -> KHÔNG ĐỔI, đầy đủ 100% (xem mục 0), vẫn là thư viện driver AD9361/no-OS thật
```

Không có gì khác bị xoá thêm so với đợt 2 (`platform.c/.h`,
`platform_config.h`, `board_app.cpp` vẫn đã xoá từ trước).

## 4. Include path - BẮT BUỘC thêm `ad9361/src` (đã xác nhận bằng lỗi build thật của bạn)

Đợt 2 tôi đoán "nhiều khả năng include path đã ở cấp project" - **sai, bạn
đã build thử và gặp đúng lỗi tôi lo**:

```
fatal error: app_config.h: No such file or directory   main.c   line 52
```

Nghĩa là project thật của bạn CHƯA có `ad9361/src` trong include path.
Đây là lỗi có thể tái hiện và đã kiểm chứng lại bằng compile thật (không
phải suy đoán): dựng lại đúng cấu trúc thư mục `src/` + `ad9361/src/` +
`noos/`, compile KHÔNG có `-I ad9361/src` -> lỗi giống hệt; compile CÓ `-I
ad9361/src` -> qua sạch.

**Đã giảm nhẹ một phần trong code**: `main.c` giờ include bằng đường dẫn
tương đối rõ ràng `#include "../ad9361/src/app_config.h"` thay vì
`#include "app_config.h"` - dòng này của `main.c` giờ KHÔNG còn phụ thuộc
include path của project nữa (đã build thử không có `-I ad9361/src`, qua
sạch).

**Nhưng việc này KHÔNG giải quyết hết** - bạn vẫn PHẢI thêm `ad9361/src`
vào include path của project, vì file vendor `ad9361_conv.c`
(`noos/drivers/rf-transceiver/ad9361/ad9361_conv.c`, không được sửa) có
`#include "app_config.h"` dạng include thường (không phải đường dẫn tương
đối cố định như 3 file vendor kia), chỉ resolve được qua include path của
project - và file này CHẮC CHẮN được compile+link, vì `ad9361_init()` gọi
thẳng vào `ad9361_post_setup()` định nghĩa trong đó (xem
`noos/drivers/rf-transceiver/ad9361/ad9361_api.c` dòng ~571). Đã tái hiện
đúng lỗi này bằng compile thật ở trên. Đây là code vendor, không được sửa
theo đúng nguyên tắc dự án, nên cách sửa duy nhất là cấu hình include path.

**Cách thêm trong Vitis Classic (Eclipse CDT)**:
1. Chuột phải vào project `5e_hte_app` -> **C/C++ Build Settings** (hoặc
   Properties -> C/C++ Build -> Settings).
2. Tab **Tool Settings** -> mục compiler (ví dụ "ARM v8 gcc compiler" /
   "ARM v8 Cortex-A53 GCC Compiler") -> **Include paths (-I)**.
3. Bấm **Add...**, nhập đường dẫn workspace-relative:
   `"${workspace_loc:/${ProjName}/ad9361/src}"`
4. Ở góc trên hộp thoại Properties, đổi dropdown **Configuration** thành
   **[ All configurations ]** trước khi bấm Add (hoặc làm lại 1 lần nữa cho
   cả Debug và Release) - để áp dụng cho cả 2 cấu hình build.
5. Apply -> Apply and Close -> Clean Project -> Build Project.

Ngoài `ad9361/src`, project vẫn cần các include path sau (thường đã có sẵn
từ trước vì `ad9361/src/main.c` gốc cũng cần chúng):
`noos/include`, `noos/drivers/api`, `noos/drivers/platform/xilinx`,
`noos/drivers/rf-transceiver/ad9361`, `noos/drivers/axi_core/axi_adc_core`,
`noos/drivers/axi_core/axi_dac_core`.

## 5. Đã tự kiểm chứng thế nào (đợt này làm kỹ hơn, tách 2 bước rõ ràng)

**Bước A - kiểm tra riêng các tính năng KHÔNG liên quan AD9361** (theo đúng
yêu cầu "build sạch cái khác trước"): compile `board_app.c`,
`device_power.c`, `device_attenuator.c`, `device_optical_delay.c`,
`device_adc7928.c`, `cmd_dispatcher.c`, `vip_helper.c` với `-Wall -Wextra`,
độc lập hoàn toàn với AD9361 -> **0 lỗi, 0 cảnh báo** sau khi sửa 2 vấn đề
thật tìm thấy trong lúc này:
- `board_app.c`, `device_power.c`, `device_attenuator.c` include
  `<unistd.h>` để lấy `usleep()` - đây là header POSIX, KHÔNG tồn tại theo
  cách đó trên BSP standalone của Xilinx. Bản thân code vendor
  (`noos/drivers/platform/xilinx/xilinx_delay.c`) và cả
  `ad9361/src/main.c` tham khảo đều dùng `#include <sleep.h>`/`"sleep.h"`
  (header thật của Xilinx BSP) cho đúng hàm này - đã sửa cả 3 file sang
  `"sleep.h"` cho nhất quán và đúng với BSP thật.
- `kria_helper.h`: điều kiện `rxmsg->len > KRIA_MAX_PAYLOAD_LEN` không bao
  giờ đúng vì `len` là `uint8_t` (0-255) và `KRIA_MAX_PAYLOAD_LEN` = 255 -
  dead code do gcc `-Wtype-limits` chỉ ra chính xác. Đã bỏ nhánh này (không
  đổi hành vi, vì nó không bao giờ chạy được) và ghi chú lại lý do.
  Link + chạy thử độc lập (không đụng gì tới AD9361) -> chạy đúng, in ra
  đúng chuỗi khởi tạo board-app.

**Bước B - build đầy đủ `main.c` mới (AD9361 gộp vào) + toàn bộ stack
board-app**, với include path đúng như mục 4 ở trên (bao gồm cả
`ad9361/src` cho `app_config.h`, để bài test này tự nó đi qua đúng đường
dẫn mà Vitis thật sẽ đi qua) -> **0 lỗi, 0 cảnh báo**. Link + chạy thử (các
hàm no-OS AD9361 nặng - `ad9361_init`, `ad9361_set_*_fir_config`,
`axi_dac_init`,... - được thay bằng bản giả tối thiểu trả về thành công, vì
bản thân driver ~237KB đó là code vendor không đổi, dựng đủ stub Xilinx BSP
để link toàn bộ nó vượt quá phạm vi hợp lý của việc kiểm tra này) -> ra
đúng:

```
AD9361: LO TX OFF
AD9361: LO RX frequency = 2440000000 Hz
AD9361: RX1 gain = 0 dB
AD9361: TX sampling frequency = 60000000 Hz
AD9361: RX RF bandwidth = 45000000 Hz
AD9361: khoi tao xong.
RS422/PC: RX bang polling (khong co ngat kha dung trong xparameters.h).
5E-HTE board-app (AC/DC/ATT/Delay/ADC7928) san sang.
```
(sau đó vào vòng lặp `BoardApp_PollOnce()` vô hạn như firmware thật - test
bị dừng bằng timeout, không phải crash.)

Việc này chứng minh: toàn bộ file tôi viết/sửa (main.c, board_app.c,
device_*.c, cmd_dispatcher.c, vip_helper.c) khớp nhau về kiểu dữ liệu, tên
hàm, include path - không thiếu biến/hàm nào. Nó KHÔNG chứng minh bản thân
driver AD9361 thật (`ad9361.c`, `ad9361_api.c`, code ADI không đổi) build
sạch trong Vitis thật với toolchain ARM thật - việc đó cần chính Vitis, và
đây là code vendor có sẵn, không phải phần được thêm/sửa ở đây. Điều mục
này bổ sung so với đợt 2: đã chứng minh bằng compile thật (không chỉ đọc
code) rằng đường include app_config.h giờ đi đúng chỗ.

## 6. Việc cần bạn xác nhận (không đổi so với đợt trước)

- Tốc độ lấy mẫu/băng thông AD9361: hiện là mặc định 60 Msps/45MHz của ADI,
  chưa chắc đúng cho "2.4GHz lấy mẫu trực tiếp" của bạn - sửa ở 2 lời gọi
  `ad9361_set_tx/rx_sampling_freq()` và 2 trường `rf_rx/tx_bandwidth_hz`
  trong `main.c` nếu cần.
- Hệ thống có chạy 2 lõi A53 cần đồng bộ không - đã bỏ cờ `A53_1_INIT_DONE`
  từ đợt 2 (giả định bare-metal đơn lõi), nói tôi nếu cần thêm lại.
