# GWU-Sat Digipeater

A digipeater acts as a radio station specifically tailored towards ham radio operators operating on the 2m band and utilizing the Automated Packet Reporting System. 
This digipeater does some things differently. 

| Settings | Normal Digipeater | GWU-Sat |
| -------- | -------- | -------- |
| Frequency | 144.39 MHz | 436.42 MHz |
| Bit Rate | Usually 1200 | Variable |
| Modulation | AFSK | CSS |

While these settings are different it still offers the same behaviors of a normal digipeating station capturing messages and repeating them. 
The reseaoning for the biggest change of our system (frequency modulation) is decided by the lora board we decided to use. 
Modern micro controllers already use digital keys thus there is no need to convert from audio to digital like older digipeaters. 


<img width="640" height="480" alt="GWU_SAT_FP Medium" src="https://github.com/user-attachments/assets/c813bb09-af1f-4824-8496-8db4a8ee395f" />

