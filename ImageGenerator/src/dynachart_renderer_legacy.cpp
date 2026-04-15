// 旧式渲染逻辑实现
// 对应之前的渲染方式：HOLD 不叠黑色背景，所有 note 都是 copyTo 叠放

#include "dynachart_renderer.h"
#include <algorithm>

/**
 * @brief 旧式渲染逻辑 - 所有音符直接 copyTo 叠放
 * 
 * 渲染特点：
 * - HOLD 音符不叠黑色背景
 * - NORMAL 和 CHAIN 音符绘制到中间层，然后整体粘贴到 board
 * - 后绘制的音符覆盖先绘制的
 */
void DynachartRenderer::drawNotesLegacy(cv::Mat& board, const PageLayout& layout,
                                        const std::vector<RenderNote>& notes,
                                        const Options& options,
                                        const std::function<void(int, int)>& progressCallback) {
    double barHeight = layout.barHeight * options.scale;
    
    // 创建中间层用于 NORMAL 和 CHAIN 音符
    cv::Mat noteFront = cv::Mat::zeros(board.size(), CV_8UC4);
    
    int totalNotes = notes.size();
    int processedNotes = 0;
    
    for (const auto& note : notes) {
        // 计算每个单位的宽度
        double widthPerUnit = note.width / 2.0;
        if (note.side == 0) {  // FRONT
            widthPerUnit *= FRONT_NOTE_RATE;
        }
        
        // 计算 X 坐标
        double x;
        if (note.side == -1) {  // LEFT
            x = (SIDE_VISIBLE_CAP - note.pos) * BOARD_SIZE * options.scale;
        } else if (note.side == 0) {  // FRONT
            double noteXInBoard = (note.pos - FRONT_LEFT_BORDER) * BOARD_SIZE * FRONT_BOARD_RATE * options.scale;
            x = layout.sideLineLeftX + noteXInBoard;
        } else {  // RIGHT
            x = layout.sideLineRightX + (note.pos - SIDE_BORDER) * BOARD_SIZE * options.scale;
        }
        
        // 跳过完全在画面外的音符
        if (x < -100 || x > board.cols + 100) continue;
        
        int pageNumber = static_cast<int>(note.start / options.timeLimit);
        int endPageNumber = pageNumber;
        if (note.type == 2) {  // HOLD
            endPageNumber = static_cast<int>(note.end / options.timeLimit);
        }
        
        // 生成音符图像
        cv::Mat noteImg = generateNoteImage(note, widthPerUnit, barHeight, options);
        
        if (noteImg.empty()) continue;
        
        // 更新进度
        processedNotes++;
        if (progressCallback) {
            progressCallback(processedNotes, totalNotes);
        }
        
        double widthHold2 = std::max(1.0, std::round(options.scale * NOTE_WIDTH_HOLD / 2.0));
        
        if (pageNumber == endPageNumber) {
            // 单页音符
            x += pageNumber * layout.pageWidth;
            double y = layout.bottomLineY - (note.start - pageNumber * options.timeLimit) * barHeight;
            int realX = static_cast<int>(x - noteImg.cols / 2.0);
            int realY;
            
            if (note.type == 2) {  // HOLD
                realY = static_cast<int>(y - noteImg.rows + widthHold2);
            } else {
                realY = static_cast<int>(y - noteImg.rows / 2.0);
            }
            
            // 提取 alpha 通道作为 mask
            cv::Mat mask;
            if (noteImg.channels() == 4) {
                std::vector<cv::Mat> channels;
                cv::split(noteImg, channels);
                mask = channels[3];
            }
            
            // 边界检查
            int clippedRealX = std::max(0, std::min(realX, board.cols - 1));
            int clippedRealY = std::max(0, std::min(realY, board.rows - 1));
            int clippedWidth = std::min(noteImg.cols, std::max(0, board.cols - clippedRealX));
            int clippedHeight = std::min(noteImg.rows, std::max(0, board.rows - clippedRealY));
            
            if (clippedWidth > 0 && clippedHeight > 0) {
                cv::Rect srcRect(std::max(0, -realX), std::max(0, -realY), clippedWidth, clippedHeight);
                cv::Rect dstRect(clippedRealX, clippedRealY, clippedWidth, clippedHeight);
                
                // 旧式渲染：根据音符类型选择目标层
                if (note.type == 2) {  // HOLD 音符直接画到 board
                    noteImg(srcRect).copyTo(board(dstRect), mask(srcRect));
                } else {  // NORMAL 和 CHAIN 画到中间层
                    noteImg(srcRect).copyTo(noteFront(dstRect), mask(srcRect));
                }
            }
        } else {
            // 跨页 HOLD 音符处理
            int capY = board.rows - static_cast<int>(layout.bottomLineY);
            int endY = static_cast<int>(layout.bottomLineY - (note.end - endPageNumber * options.timeLimit) * barHeight - widthHold2);
            int startClip = static_cast<int>(((pageNumber + 1) * options.timeLimit - note.start) * barHeight + widthHold2);
            int endClip = static_cast<int>((note.end - endPageNumber * options.timeLimit) * barHeight + widthHold2);
            
            startClip = std::max(1, startClip);
            endClip = std::max(1, endClip);
            
            int startRectY = std::max(0, noteImg.rows - startClip);
            int startRectH = noteImg.rows - startRectY;
            
            int endRectH = std::min(endClip, noteImg.rows);
            
            if (startRectH > 0 && endRectH > 0) {
                cv::Rect startRect(0, startRectY, noteImg.cols, startRectH);
                cv::Rect endRect(0, 0, noteImg.cols, endRectH);
                
                if (startRect.x >= 0 && startRect.y >= 0 && startRect.width > 0 && startRect.height > 0 &&
                    startRect.x + startRect.width <= noteImg.cols && startRect.y + startRect.height <= noteImg.rows &&
                    endRect.x >= 0 && endRect.y >= 0 && endRect.width > 0 && endRect.height > 0 &&
                    endRect.x + endRect.width <= noteImg.cols && endRect.y + endRect.height <= noteImg.rows) {
                    
                    cv::Mat startCrop = noteImg(startRect).clone();
                    cv::Mat endCrop = noteImg(endRect).clone();
                    
                    // 提取 alpha 通道
                    cv::Mat startMask, endMask;
                    if (startCrop.channels() == 4) {
                        std::vector<cv::Mat> channels;
                        cv::split(startCrop, channels);
                        startMask = channels[3];
                    }
                    if (endCrop.channels() == 4) {
                        std::vector<cv::Mat> channels;
                        cv::split(endCrop, channels);
                        endMask = channels[3];
                    }
                    
                    int startX = static_cast<int>(x + pageNumber * layout.pageWidth - noteImg.cols / 2.0);
                    int endX = static_cast<int>(x + endPageNumber * layout.pageWidth - noteImg.cols / 2.0);
                    
                    int startDstX = std::max(0, std::min(startX, board.cols - startCrop.cols));
                    int endDstX = std::max(0, std::min(endX, board.cols - endCrop.cols));
                    
                    // 粘贴 start 部分
                    if (capY >= 0 && capY + startCrop.rows <= board.rows) {
                        cv::Rect dstRect(startDstX, capY, startCrop.cols, startCrop.rows);
                        if (dstRect.x + dstRect.width <= board.cols && dstRect.y + dstRect.height <= board.rows) {
                            // HOLD 音符直接画到 board
                            startCrop.copyTo(board(dstRect), startMask.empty() ? cv::noArray() : startMask);
                        }
                    }
                    
                    // 粘贴 end 部分
                    int actualEndY = std::max(0, std::min(endY, board.rows - endCrop.rows));
                    if (endY >= 0 && endY + endCrop.rows <= board.rows) {
                        cv::Rect dstRect(endDstX, actualEndY, endCrop.cols, endCrop.rows);
                        if (dstRect.x + dstRect.width <= board.cols && dstRect.y + dstRect.height <= board.rows) {
                            // HOLD 音符直接画到 board
                            endCrop.copyTo(board(dstRect), endMask.empty() ? cv::noArray() : endMask);
                        }
                    }
                    
                    // 处理中间页面
                    double pagesize = options.timeLimit * barHeight;
                    for (int pg = pageNumber + 1; pg < endPageNumber; pg++) {
                        double cy = noteImg.rows - startClip - (pg - pageNumber) * pagesize;
                        int cropY = static_cast<int>(cy);
                        int cropH = static_cast<int>(pagesize);
                        
                        if (cropY < 0) cropY = 0;
                        if (cropY + cropH > noteImg.rows) cropH = noteImg.rows - cropY;
                        if (cropH <= 0) continue;
                        
                        cv::Rect cropRect(0, cropY, noteImg.cols, cropH);
                        cv::Mat crop = noteImg(cropRect).clone();
                        
                        cv::Mat cropMask;
                        if (crop.channels() == 4) {
                            std::vector<cv::Mat> channels;
                            cv::split(crop, channels);
                            cropMask = channels[3];
                        }
                        
                        int pgX = static_cast<int>(x + pg * layout.pageWidth - noteImg.cols / 2.0);
                        int pgDstX = std::max(0, std::min(pgX, board.cols - crop.cols));
                        
                        if (capY >= 0 && capY + crop.rows <= board.rows) {
                            cv::Rect dstRect(pgDstX, capY, crop.cols, crop.rows);
                            if (dstRect.x + dstRect.width <= board.cols && dstRect.y + dstRect.height <= board.rows) {
                                // HOLD 音符直接画到 board
                                crop.copyTo(board(dstRect), cropMask.empty() ? cv::noArray() : cropMask);
                            }
                        }
                    }
                }
            }
        }
    }
    
    // 最后将中间层（NORMAL 和 CHAIN）整体粘贴到 board 上
    // 使用 alpha 通道作为 mask，只复制非透明部分，不覆盖背景
    cv::Mat noteFrontMask;
    if (noteFront.channels() == 4) {
        std::vector<cv::Mat> channels;
        cv::split(noteFront, channels);
        noteFrontMask = channels[3];
    }
    noteFront.copyTo(board, noteFrontMask);
}
