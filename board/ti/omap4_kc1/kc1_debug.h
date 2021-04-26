#ifndef __KC1_DEBUG_H
#define __KC1_DEBUG_H

// !!!! CHECK CFG_HZ=150000, V_SCLK=38400000, CFG_PVT=7, (2 << CFG_PVT)=256

void dump_gpio(unsigned int base);
int dump_clock_control(void);
int dump_dss(void);
int dump_musb(void);
int dump_control_general_core(void);
int dump_control_general_wkup(void);
int dump_control_padconf_core(void);
int dump_control_padconf_wkup(void);
void dump_mcspi(unsigned int base);
int dump_i2c_twl6030(void);
int dump_i2c_smb347(void);
int dump_prcm(void);
void dump_gptimer_1_2_10(unsigned int base);
void dump_gptimer_3_4_5_6_7_8_9_11(unsigned int base);

#endif /* __KC1_BOARD_H */
