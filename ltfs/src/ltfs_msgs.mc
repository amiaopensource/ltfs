;/*
; This MC file allows us to suppress the warnings in the event log 
; about there being no provider for our messages.
;
; To compile this MC file and generate a new RC file, use the following
; command:
; 
; mc.exe ltfs_msgs.mc
;*/
MessageIdTypedef=DWORD

SeverityNames=(Success=0x0:STATUS_SEVERITY_SUCCESS
               Informational=0x1:STATUS_SEVERITY_INFORMATIONAL
               Warning=0x2:STATUS_SEVERITY_WARNING
               Error=0x3:STATUS_SEVERITY_ERROR
              )

MessageId=100 
SymbolicName=LTFS_ERROR_EVENT
Language=English
The LTFS file system encountered an error
.


