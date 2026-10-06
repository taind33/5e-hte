# Đồng bộ firmware 5E-HTE ↔ app Qt DashboardEnv — 2026-10-01

Đầu vào: `5e_hte_app_new.zip` (firmware, bản anh build trong Vitis ngày 11/09, GCC 11.2) và `DashboardEnv_real.zip` (app Qt 6.11.2 / MinGW 13.1).
Toàn bộ thay đổi nằm trong các file **đã có sẵn** — không có file nguồn mới, nên **không cần "Add Sources" trong Vitis**, chỉ cần build lại.

Các chỉnh sửa riêng của anh được giữ nguyên: `digital_interface_tune_skip_mode = 1`, đoạn console UART đang comment trong `main.c`, và macro baud 9600 trong `board_config.h`.

---

## 1. Lỗi nghiêm trọng tìm được (qua test end-to-end thật, không phải đọc code)

### 1.1 [App] App bỏ qua MỌI frame dữ liệu từ board

`#define VIP_NODE_ID_SELF 0x00` chỉ được đặt trong `dashboardmanager.h`, mà `vip_helper.cpp` là một translation unit riêng, không include file đó. Kết quả là `VIP_Process_Frame()` vẫn được biên dịch với SELF = 0x01, nên mọi frame board gửi cho host (desId 0x00) bị coi là "forward" và bị bỏ qua.

Hệ quả trong bản cũ:

- App không bao giờ nhận ACK của lệnh AC/DC/ATT/Delay.
- App không nhận telemetry ADC7928, nên ô kích nổ đứng yên.
- App không nhận phổ RF "WAVE". Câu trả lời trước của tôi rằng "về code đã hiển thị được phổ" là **sai trên thực tế**: chỉ riêng PING/ACK_STD (đèn xác thực) chạy được.

Cách sửa: mặc định 0x00 đặt ngay trong bản sao `vip_helper.h` của app, kèm `static_assert` ở cả `vip_helper.cpp` và `dashboardmanager.h`. Nếu sau này có ai đổi lại, build sẽ báo lỗi ngay.

### 1.2 [Firmware] Mất lệnh từ PC khi firmware đang bận

`xparameters.h` của board không nối ngắt UART RS422 nên firmware đọc RX bằng polling, trong khi FIFO phần cứng chỉ có 16 byte (khoảng 1,4 ms ở 115200 baud). Trong các khoảng chờ sau, không ai đọc FIFO nên byte tới bị tràn:

- gửi khung WAVE (khoảng 13 ms);
- gửi chuỗi lệnh nguồn AC (khoảng 250 ms usleep, cộng phần TX ở 9600 baud);
- chờ DMA (sleep cứng từng 1 ms).

Đo bằng mô phỏng thời gian ảo, gửi 90 lệnh, có xen lệnh AC:

| | ACK nhận được | byte bị tràn FIFO | khoảng hở đọc RX lớn nhất |
|---|---|---|---|
| Firmware cũ | **39/90** | 1890 | 258 ms |
| Firmware mới | **90/90** | 0 | 0,05 ms |

Cách sửa: thêm ring buffer RX 1024 byte và `BoardApp_ServiceRx()`. Hàm này được gọi trong mọi vòng chờ: TX UART, `BoardApp_DelayUs()` (thay cho `usleep` trong `device_power.c`) và lúc chờ DMA.

### 1.3 [Firmware] DMA polled đọc phải cờ cũ

`axi_dmac_transfer_wait_completion()` (no-OS) chờ `IRQ_PENDING == SOT|EOT`, nhưng chỉ ISR mới xoá thanh ghi này (W1C). Từ lần capture thứ 2 trở đi, hàm chờ trả về ngay lập tức với cờ của lần trước.

Cách sửa: xoá `IRQ_PENDING` trước mỗi lần start và tự poll hoàn tất (vẫn đọc RX trong lúc chờ, timeout 500 ms). Thêm `Xil_DCacheFlushRange` trước khi DMA ghi vào buffer.

### 1.4 [Firmware] Macro baud 9600 cho nguồn AC/DC không có tác dụng

`board_app.c` vẫn hardcode 115200. Giờ đã dùng `UART_AC_PWR_BAUDRATE` / `UART_DC_PWR_BAUDRATE`.

### 1.5 [Firmware] WAVE/ADC chỉ chạy khi đường truyền "rảnh"

Bộ đếm idle bị reset về 0 mỗi khi có byte đến, nên lưu lượng đều đặn (PING 2 s/lần, lệnh) làm thưa hẳn phổ và telemetry: firmware cũ chỉ gửi 4 khung WAVE trong 4,2 s, đáng ra phải khoảng 14.

Cách sửa: lập lịch bằng global timer `XTime`. ADC 1 s, WAVE 300 ms, RFST 3 s.

---

## 2. Suy hao F1956 — truyền đúng bước 0,25 dB

Datasheet F1956 (7 bit, 0,25 dB/bước, 0–31,75 dB) và `Attenuator_SetDb()` vốn đã xử lý số thực. Chỗ duy nhất làm mất độ phân giải là byte trên dây (dB nguyên).

- **Gói mới** `annuation_q_msg_t` (msgid 38, **7 byte**): `"ATQ"`, kênh, `uint16 value_qdb` = số bước 0,25 dB (0..127).
- **Tương thích ngược, an toàn cả hai chiều:**
  - Firmware mới vẫn nhận gói cũ 6 byte (dB nguyên).
  - Firmware cũ nhận gói 7 byte thì **NACK** do kiểm tra độ dài, nên app báo lỗi đỏ chứ không đặt nhầm gấp 4 lần suy hao. Điều này đã kiểm tra trên mô phỏng firmware cũ.
  - Giá trị ngoài 0..127 hoặc kênh khác 1..3 thì NACK, không âm thầm kẹp giá trị.
- Nút gửi (✈) giờ **luôn** gửi, kể cả khi số không đổi. Trước đây, giá trị nạp lại từ .ini lúc mở app không có cách nào gửi xuống board nếu không sửa số.

---

## 3. Đo công suất RF đầu vào RX1 — luồng hoàn chỉnh

| Firmware | App |
|---|---|
| DMA 32 mẫu I/Q RX1 → FFT 32 điểm → `WAVE` (300 ms) | Đổi sang **dBFS** (0 dBFS = sóng sin phức đầy thang 12-bit) |
| **Mới:** `RFST` (3 s): LO, Fs, BW, gain, chế độ gain, số điểm FFT — lấy từ cache lúc bring-up, **không đọc SPI AD9361 lúc chạy** (bus SPI0 dùng chung với F1956) | fftshift, trục X theo **MHz tuyệt đối** (LO ± Fs/2) |
| | **Tổng công suất trong băng** (Parseval — không bị sai số scalloping như số đỉnh) và **đỉnh @ tần số** |
| | Trạng thái rõ ràng: *Dữ liệu thật* / *Chưa nhận phổ* (watchdog 2 s) / *Mô phỏng* (chưa kết nối) |
| | **Hiệu chuẩn dBFS → dBm** (lưu vào .ini kèm LO/gain; nếu LO/gain đổi thì cảnh báo) |

Dòng chữ hardcode cũ "Freq: 2.45 GHz / Trigger: -40 dBm" (sai so với LO thật 2440 MHz, và trigger không tồn tại) đã được thay bằng số thật.

**Cách hiệu chuẩn trên phần cứng:**

1. Đưa máy phát tín hiệu chuẩn (CW, biết chính xác công suất tại đầu vào RX1, trong dải 2410–2470 MHz) vào board.
2. Chờ nhãn chuyển sang "Dữ liệu thật".
3. Nhập công suất chuẩn (dBm) vào ô cạnh nút **Hiệu chuẩn**, rồi bấm.
4. Từ đó đơn vị chuyển sang dBm. Muốn quay về dBFS thì bấm **Xoá**.

---

## 4. Các sửa nhỏ khác

**App:**

- ACK của lệnh **TẮT** AC/DC từng bật đèn ON. Giờ app nhớ trạng thái đã yêu cầu; nếu NACK thì trả công tắc về trạng thái cũ và báo lỗi.
- Công tắc nguồn dùng `onToggled` (chỉ khi người dùng bấm). Trước đây `onCheckedChanged` làm mọi thay đổi `isOn` từ backend (ACK, nút Đặt lại) **tự gửi thêm lệnh AC/DC** xuống board.
- ACK lệnh CMD hiện ở ô Terminal (dòng `[RX] ACK OK/FAIL`).
- Lỗi chính tả `"Sucess"` làm thông báo kết nối mất màu xanh.
- Khi kết nối, app xoá số liệu AC/DC mô phỏng còn sót lại. Min/Max hiện `--` khi lịch sử rỗng, thay vì `-500/500`.
- `static_assert` kích thước mọi struct trên dây ở **cả hai phía** (lệch là không build được).

**Firmware:**

- Thiết bị nào khởi tạo lỗi (F1956, GPIO delay, UART nguồn) thì lệnh tương ứng bị **NACK**. Trước đây firmware `return` sớm, cổng PC không được mở và board im lặng hoàn toàn.
- Lỗi ẩn trong `circularBuffer.h` (thiếu `{}` sau `if`, nên `buffer_end` không bao giờ được gán).
- `ad9361_api.c`: thêm `#include "xil_printf.h"` cho dòng debug PRODUCT_ID anh thêm. Hết cảnh báo implicit declaration.
- Cập nhật comment cũ: DC current x100, dải ADC 0..2·VREF.

**`tools/vip_test_client.py`:** `att` gửi bước 0,25 dB, thêm `att_legacy`, `dc` nhận dòng số thực, in giải mã WAVE/RFST.

---

## 5. Đã kiểm chứng thế nào (và giới hạn)

| Hạng mục | Công cụ | Kết quả |
|---|---|---|
| Firmware build | `aarch64 GCC 11.5` (bản gần nhất với 11.2 của Vitis), **cùng cờ, cùng thứ tự `-I`, cùng danh sách 27 file** như `Debug/makefile`, BSP header dựng lại theo đúng prototype Xilinx 2022.x. Bộ header này tái tạo **đúng y hệt** các cảnh báo vendor trong log build Vitis của anh (cùng file, cùng dòng). | 27/27 file. `src/` **0 cảnh báo** (kể cả với `-Wextra`). Chỉ còn cảnh báo vendor no-OS như cũ. Symbol ngoài mới duy nhất cần libxil: `Xil_DCacheFlushRange`. |
| App C++ | CMake build thật với `-Wall -Wextra` (Qt 6.4 headers trong sandbox, vì không tải được SDK 6.11.2) | 0 cảnh báo, 0 lỗi |
| QML | **Qt 6.11.2 thật** (từ wheel PySide6 6.11.2): `qmlcachegen` (AOT, đúng bước build của anh), `qmllint`, và **chạy/render runtime** toàn bộ `Main.qml` | 18/18 file biên dịch được, 0 lỗi lint mới, render 0 cảnh báo |
| End-to-end | **Backend C++ thật** ↔ cổng nối tiếp ảo (pty) ↔ **mã nguồn firmware thật** chạy trên mô phỏng phần cứng (UART 16-byte FIFO theo baud, DMA AXI-DMAC theo thanh ghi, F1956/GPIO/ADC7928) | 20/20 kiểm tra đạt: xác thực PING, WAVE, RFST, tone -20 dBFS @ 2445,625 MHz đúng tới ±0,02 dB, ADC, ACK AC/DC/ATT/CMD, ATT 12,75 dB → F1956 nhận 51 bước, hiệu chuẩn -30 dBm, RESET |
| GUI end-to-end | `Main.qml` thật + backend thật + firmware mô phỏng, chụp màn hình | xem `verify_sync/*.png` |

**Giới hạn — chưa phải phần cứng thật:**

- Mô phỏng chỉ mô hình hoá giao tiếp (UART/DMA/SPI/GPIO). Nó **không** kiểm chứng được:
  - AD9361 thực sự lấy mẫu đúng;
  - layout buffer I1,Q1,I2,Q2;
  - DMA có cần tín hiệu sync từ HDL hay không;
  - thời gian tính FFT thật trên A53.
- Phần C++ chưa được build bằng chính Qt 6.11.2/MinGW 13.1. Code chỉ dùng API Qt ổn định từ 6.2, nhưng anh vẫn cần build lại trên máy.

---

## 6. Cách áp dụng

1. **Firmware:** chép đè `src/*`, `noos/drivers/rf-transceiver/ad9361/ad9361_api.c` và `tools/vip_test_client.py` vào `5e_hte_app`, sau đó **Clean → Build**, rồi nạp lại board.
2. **App:** chép đè, **xoá thư mục `build/`** (tránh lỗi chạy bản cũ như lần trước), rồi **Rebuild**.
3. **Nạp cả hai cùng lúc.** App mới chạy với firmware cũ vẫn an toàn (ATT bị NACK, phổ hiện theo bin, không có MHz), nhưng chỉ khi cả hai cùng mới thì mới đủ chức năng.
4. Khi chạy thật, mở file log phiên (`Documents/DashboardLogs`). Các dòng `[RF] AD9361 RX: LO …` và `[RF] Bắt đầu nhận phổ RF…` xác nhận luồng phổ đã chạy.

---

## 7. Cần anh xác nhận / bổ sung

1. **Tần số và mức công suất RF anh đưa vào.**
   - Firmware đang cố định LO 2440 MHz, Fs 60 MSPS, nên chỉ nhìn thấy dải **2410–2470 MHz**.
   - Gain RX đang là **0 dB, chế độ MGC**.
   - Nếu tín hiệu nằm ngoài dải này, hoặc quá yếu hay quá mạnh, cần đổi LO/gain. Anh có muốn đặt LO/gain từ app không? Việc này cần ghi SPI vào AD9361 lúc đang chạy, trên đúng bus SPI0 dùng chung với F1956, nên cần thiết kế cẩn thận.
2. **Baud 9600** của nguồn AC/DC (lấy theo macro anh thêm) — có đúng với tài liệu nguồn không?
3. Nguồn AC/DC có lệnh đọc ngược điện áp/dòng (ví dụ `MEAS?`) không? Hiện UART nguồn chỉ phát lệnh, nên 4 biểu đồ AC/DC khi đã kết nối chỉ thể hiện **giá trị đặt**, không phải số đo.
4. `xparameters.h` tôi dùng (UART RS422 **không** có ngắt) có còn là bản hiện tại của bitstream không?
5. FFT 32 điểm (1,875 MHz/bin, cửa sổ chữ nhật) có đủ không, hay cần nhiều điểm hơn / cửa sổ Hann? Nếu cần nhiều điểm hơn thì phải tách nhiều frame, vì một payload tối đa 62 điểm.
