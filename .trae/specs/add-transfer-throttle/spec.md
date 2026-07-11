# Progress Bar Visibility & Transfer Throttle Spec

## Why
文件传输循环是一个无延迟的 tight loop，且进度更新被 2% 阈值过滤，导致小文件传输时进度条几乎不动，用户无法看到传输进度。需要加入传输节流延迟并降低进度更新阈值，让进度条可见地跟随传输进度变化。

## What Changes
- 在 `handle_download_chunk()` 和 `handle_upload_chunk()` 中加入 `usleep()` 延迟，减缓传输速度
- 降低进度更新阈值（从 2% 降到 1%），让更多进度更新通过
- 确认 Close/Cancel 按钮行为正确（已有实现，仅需验证）

## Impact
- Affected code:
  - `client/network_task.c` — 传输节流 + 进度阈值调整
- 不需要修改的文件：
  - `client/ui_manager.c` — 弹窗 UI 和按钮回调已正确实现

## ADDED Requirements

### Requirement: Transfer Throttle
系统 SHALL 在每个 chunk 传输后加入短延迟，减缓传输速度，使进度条可见地变化。

#### Scenario: Download Chunk Throttle
- **WHEN** 网络线程处理一个下载 chunk
- **THEN** 在写入文件后、返回前，调用 `usleep(CHUNK_DELAY_US)` 暂停
- **AND** 延迟时间由宏 `CHUNK_DELAY_US` 控制（建议 5000-10000 微秒）

#### Scenario: Upload Chunk Throttle
- **WHEN** 网络线程处理一个上传 chunk
- **THEN** 在发送到 socket 后、返回前，调用 `usleep(CHUNK_DELAY_US)` 暂停

### Requirement: Lower Progress Update Threshold
系统 SHALL 将进度更新阈值从 2% 降低到 1%。

#### Scenario: More Frequent Updates
- **WHEN** 传输进度变化达到 1%
- **THEN** 系统发送 `lv_async_call` 更新 UI 进度条
- **AND** 不再被 `if (pct - g_last_progress < 2 && pct < 100) return;` 过滤

## MODIFIED Requirements

### Requirement: Download Progress Update
`update_dl_progress()` 中的过滤条件 SHALL 从 `pct - g_last_progress < 2` 改为 `pct - g_last_progress < 1`。

**Previous**: `if (pct - g_last_progress < 2 && pct < 100) return;`
**New**: `if (pct - g_last_progress < 1 && pct < 100) return;`

### Requirement: Upload Progress Update
`update_ul_progress()` 中的过滤条件 SHALL 从 `pct - g_last_progress < 2` 改为 `pct - g_last_progress < 1`。

**Previous**: `if (pct - g_last_progress < 2 && pct < 100) return;`
**New**: `if (pct - g_last_progress < 1 && pct < 100) return;`

### Requirement: Close Button Behavior (Verified, No Change)
点击 Close 按钮 SHALL 仅关闭进度弹窗，不影响传输。
- `on_close_progress_btn_clicked` 销毁 `batch_prog_panel` / `prog_panel`，传输继续

### Requirement: Cancel Button Behavior (Verified, No Change)
点击 Cancel 按钮 SHALL 取消传输并关闭弹窗。
- `on_cancel_btn_clicked` 调用 `network_cancel_transfer()` + `ui_hide_progress()` + 显示 "Transfer cancelled"

## REMOVED Requirements
无
