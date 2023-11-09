- [ ] packet ack
- [ ] stream unit test
- [ ] ack packet enc
- [ ] manage conection id
- [ ] bbr

管理发送表，定时重传的逻辑，数据包要能在发送前进行序列化
1. 创建send manager来管理发送控制
2. 创建recv manager来管理接收控制
3. 创建send window 实现发送滑动窗口控制