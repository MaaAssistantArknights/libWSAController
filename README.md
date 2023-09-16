# libWSAController
用于WSA窗口的输入控制和画面获取。
## 文件介绍
画面获取使用Windows Graphics Capture技术。输入控制分为鼠标消息注入和触摸消息注入，主要通过VirtualTouch.cpp、Inputs.cpp两个文件实现。
### VirutalTouch.cpp
 - 进行GetMessage、GetPointerType、GetPointerTouchInfo\GetPointerPenInfo和GetKeyState函数的hook
 - 通过将消息内存写入目标进程将WM_POINTERXX消息通过hook的GetMessage函数注入目标窗口
### Inputs.cpp
 - 将用户需要的输入消息进行整理并和分类。
 - 对每一个WSA窗口维护一个消息处理线程，将分类后的消息解析后通过VirtualTouch.cpp实现输入。
### Capture.cpp
 - 捕获窗口内容
 - contiguous参数控制是否进行持续不断的窗口捕获以实时获取最新的窗口更新消息。
### ManageWindow.cpp
 - 负责将所有的WSA窗口移动到屏幕外。
 - 创建Windows窗口转发所有用户实际进行的输入。
