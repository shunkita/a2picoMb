/* Author: Peter Sovietov */

#include <string.h>
#include <math.h>
#include "ayumi.h"

static const float AY_dac_table[] = {
  0.0f, 0.0f,
  0.00999465934234f, 0.00999465934234f,
  0.0144502937362f, 0.0144502937362f,
  0.0210574502174f, 0.0210574502174f,
  0.0307011520562f, 0.0307011520562f,
  0.0455481803616f, 0.0455481803616f,
  0.0644998855573f, 0.0644998855573f,
  0.107362478065f, 0.107362478065f,
  0.126588845655f, 0.126588845655f,
  0.20498970016f, 0.20498970016f,
  0.292210269322f, 0.292210269322f,
  0.372838941024f, 0.372838941024f,
  0.492530708782f, 0.492530708782f,
  0.635324635691f, 0.635324635691f,
  0.805584802014f, 0.805584802014f,
  1.0f, 1.0f
};

static const float YM_dac_table[] = {
  0.0f, 0.0f,
  0.00465400167849f, 0.00772106507973f,
  0.0109559777218f, 0.0139620050355f,
  0.0169985503929f, 0.0200198367285f,
  0.024368657969f, 0.029694056611f,
  0.0350652323186f, 0.0403906309606f,
  0.0485389486534f, 0.0583352407111f,
  0.0680552376593f, 0.0777752346075f,
  0.0925154497597f, 0.111085679408f,
  0.129747463188f, 0.148485542077f,
  0.17666895552f, 0.211551079576f,
  0.246387426566f, 0.281101701381f,
  0.333730067903f, 0.400427252613f,
  0.467383840696f, 0.53443198291f,
  0.635172045472f, 0.75800717174f,
  0.879926756695f, 1.0f
};

static void reset_segment(struct ayumi* ay);

static int update_tone(struct ayumi* ay, int index) {
  struct tone_channel* ch = &ay->channels[index];
  ch->tone_counter += 1;
  if (ch->tone_counter >= ch->tone_period) {
    ch->tone_counter = 0;
    ch->tone ^= 1;
  }
  return ch->tone;
}

static int update_noise(struct ayumi* ay) {
  int bit0x3;
  ay->noise_counter += 1;
  if (ay->noise_counter >= (ay->noise_period << 1)) {
    ay->noise_counter = 0;
    bit0x3 = ((ay->noise ^ (ay->noise >> 3)) & 1);
    ay->noise = (ay->noise >> 1) | (bit0x3 << 16);
  }
  return ay->noise & 1;
}

static void slide_up(struct ayumi* ay) {
  ay->envelope += 1;
  if (ay->envelope > 31) {
    ay->envelope_segment ^= 1;
    reset_segment(ay);
  }
}

static void slide_down(struct ayumi* ay) {
  ay->envelope -= 1;
  if (ay->envelope < 0) {
    ay->envelope_segment ^= 1;
    reset_segment(ay);
  }
}

static void hold_top(struct ayumi* ay) {
  (void) ay;
}

static void hold_bottom(struct ayumi* ay) {
  (void) ay;
}

static void (* const Envelopes[][2])(struct ayumi*) = {
  {slide_down, hold_bottom},
  {slide_down, hold_bottom},
  {slide_down, hold_bottom},
  {slide_down, hold_bottom},
  {slide_up, hold_bottom},
  {slide_up, hold_bottom},
  {slide_up, hold_bottom},
  {slide_up, hold_bottom},
  {slide_down, slide_down},
  {slide_down, hold_bottom},
  {slide_down, slide_up},
  {slide_down, hold_top},
  {slide_up, slide_up},
  {slide_up, hold_top},
  {slide_up, slide_down},
  {slide_up, hold_bottom}
};

static void reset_segment(struct ayumi* ay) {
  if (Envelopes[ay->envelope_shape][ay->envelope_segment] == slide_down
    || Envelopes[ay->envelope_shape][ay->envelope_segment] == hold_top) {
    ay->envelope = 31;
    return;
  }
  ay->envelope = 0;
}

int update_envelope(struct ayumi* ay) {
  ay->envelope_counter += 1;
  if (ay->envelope_counter >= ay->envelope_period) {
    ay->envelope_counter = 0;
    Envelopes[ay->envelope_shape][ay->envelope_segment](ay);
  }
  return ay->envelope;
}

static void update_mixer(struct ayumi* ay) {
  int i;
  int out;
  int noise = update_noise(ay);
  int envelope = update_envelope(ay);
  float sum = 0.0f;
  for (i = 0; i < TONE_CHANNELS; i += 1) {
    out = (update_tone(ay, i) | ay->channels[i].t_off) & (noise | ay->channels[i].n_off);
    out *= ay->channels[i].e_on ? envelope : ay->channels[i].volume * 2 + 1;
    sum += ay->dac_table[out] * ay->channels[i].pan_left;
  }
  ay->left = sum;
  ay->right = sum;
}

int ayumi_configure(struct ayumi* ay, int is_ym, float clock_rate, int sr) {
  int i;
  memset(ay, 0, sizeof(struct ayumi));
  ay->step = clock_rate / (sr * 8.0f * DECIMATE_FACTOR);
  ay->dac_table = is_ym ? YM_dac_table : AY_dac_table;
  ay->noise = 1;
  ayumi_set_envelope(ay, 1);
  for (i = 0; i < TONE_CHANNELS; i += 1) {
    ayumi_set_tone(ay, i, 1);
  }
  return ay->step < 1.0f;
}

void ayumi_set_pan(struct ayumi* ay, int index, float pan, int is_eqp) {
  if (is_eqp) {
    ay->channels[index].pan_left = sqrtf(1.0f - pan);
    ay->channels[index].pan_right = sqrtf(pan);
  } else {
    ay->channels[index].pan_left = 1.0f - pan;
    ay->channels[index].pan_right = pan;
  }
}

void ayumi_set_tone(struct ayumi* ay, int index, int period) {
  period &= 0xfff;
  ay->channels[index].tone_period = (period == 0) | period;
}

void ayumi_set_noise(struct ayumi* ay, int period) {
  period &= 0x1f;
  ay->noise_period = (period == 0) | period;
}

void ayumi_set_mixer(struct ayumi* ay, int index, int t_off, int n_off, int e_on) {
  ay->channels[index].t_off = t_off & 1;
  ay->channels[index].n_off = n_off & 1;
  ay->channels[index].e_on = e_on;
}

void ayumi_set_volume(struct ayumi* ay, int index, int volume) {
  ay->channels[index].volume = volume & 0xf;
}

void ayumi_set_envelope(struct ayumi* ay, int period) {
  period &= 0xffff;
  ay->envelope_period = (period == 0) | period;
}

void ayumi_set_envelope_shape(struct ayumi* ay, int shape) {
  ay->envelope_shape = shape & 0xf;
  ay->envelope_counter = 0;
  ay->envelope_segment = 0;
  reset_segment(ay);
}

static float decimate(float* x) {
  float y = -0.0000046183113992051936f * (x[1] + x[191]) +
    -0.00001117761640887225f * (x[2] + x[190]) +
    -0.000018610264502005432f * (x[3] + x[189]) +
    -0.000025134586135631012f * (x[4] + x[188]) +
    -0.000028494281690666197f * (x[5] + x[187]) +
    -0.000026396828793275159f * (x[6] + x[186]) +
    -0.000017094212558802156f * (x[7] + x[185]) +
    0.000023798193576966866f * (x[9] + x[183]) +
    0.000051281160242202183f * (x[10] + x[182]) +
    0.00007762197826243427f * (x[11] + x[181]) +
    0.000096759426664120416f * (x[12] + x[180]) +
    0.00010240229300393402f * (x[13] + x[179]) +
    0.000089344614218077106f * (x[14] + x[178]) +
    0.000054875700118949183f * (x[15] + x[177]) +
    -0.000069839082210680165f * (x[17] + x[175]) +
    -0.0001447966132360757f * (x[18] + x[174]) +
    -0.00021158452917708308f * (x[19] + x[173]) +
    -0.00025535069106550544f * (x[20] + x[172]) +
    -0.00026228714374322104f * (x[21] + x[171]) +
    -0.00022258805927027799f * (x[22] + x[170]) +
    -0.00013323230495695704f * (x[23] + x[169]) +
    0.00016182578767055206f * (x[25] + x[167]) +
    0.00032846175385096581f * (x[26] + x[166]) +
    0.00047045611576184863f * (x[27] + x[165]) +
    0.00055713851457530944f * (x[28] + x[164]) +
    0.00056212565121518726f * (x[29] + x[163]) +
    0.00046901918553962478f * (x[30] + x[162]) +
    0.00027624866838952986f * (x[31] + x[161]) +
    -0.00032564179486838622f * (x[33] + x[159]) +
    -0.00065182310286710388f * (x[34] + x[158]) +
    -0.00092127787309319298f * (x[35] + x[157]) +
    -0.0010772534348943575f * (x[36] + x[156]) +
    -0.0010737727700273478f * (x[37] + x[155]) +
    -0.00088556645390392634f * (x[38] + x[154]) +
    -0.00051581896090765534f * (x[39] + x[153]) +
    0.00059548767193795277f * (x[41] + x[151]) +
    0.0011803558710661009f * (x[42] + x[150]) +
    0.0016527320270369871f * (x[43] + x[149]) +
    0.0019152679330965555f * (x[44] + x[148]) +
    0.0018927324805381538f * (x[45] + x[147]) +
    0.0015481870327877937f * (x[46] + x[146]) +
    0.00089470695834941306f * (x[47] + x[145]) +
    -0.0010178225878206125f * (x[49] + x[143]) +
    -0.0020037400552054292f * (x[50] + x[142]) +
    -0.0027874356824117317f * (x[51] + x[141]) +
    -0.003210329988021943f * (x[52] + x[140]) +
    -0.0031540624117984395f * (x[53] + x[139]) +
    -0.0025657163651900345f * (x[54] + x[138]) +
    -0.0014750752642111449f * (x[55] + x[137]) +
    0.0016624165446378462f * (x[57] + x[135]) +
    0.0032591192839069179f * (x[58] + x[134]) +
    0.0045165685815867747f * (x[59] + x[133]) +
    0.0051838984346123896f * (x[60] + x[132]) +
    0.0050774264697459933f * (x[61] + x[131]) +
    0.0041192521414141585f * (x[62] + x[130]) +
    0.0023628575417966491f * (x[63] + x[129]) +
    -0.0026543507866759182f * (x[65] + x[127]) +
    -0.0051990251084333425f * (x[66] + x[126]) +
    -0.0072020238234656924f * (x[67] + x[125]) +
    -0.0082672928192007358f * (x[68] + x[124]) +
    -0.0081033739572956287f * (x[69] + x[123]) +
    -0.006583111539570221f * (x[70] + x[122]) +
    -0.0037839040415292386f * (x[71] + x[121]) +
    0.0042781252851152507f * (x[73] + x[119]) +
    0.0084176358598320178f * (x[74] + x[118]) +
    0.01172566057463055f * (x[75] + x[117]) +
    0.013550476647788672f * (x[76] + x[116]) +
    0.013388189369997496f * (x[77] + x[115]) +
    0.010979501242341259f * (x[78] + x[114]) +
    0.006381274941685413f * (x[79] + x[113]) +
    -0.007421229604153888f * (x[81] + x[111]) +
    -0.01486456304340213f * (x[82] + x[110]) +
    -0.021143584622178104f * (x[83] + x[109]) +
    -0.02504275058758609f * (x[84] + x[108]) +
    -0.025473530942547201f * (x[85] + x[107]) +
    -0.021627310017882196f * (x[86] + x[106]) +
    -0.013104323383225543f * (x[87] + x[105]) +
    0.017065133989980476f * (x[89] + x[103]) +
    0.036978919264451952f * (x[90] + x[102]) +
    0.05823318062093958f * (x[91] + x[101]) +
    0.079072012081405949f * (x[92] + x[100]) +
    0.097675998716952317f * (x[93] + x[99]) +
    0.11236045936950932f * (x[94] + x[98]) +
    0.12176343577287731f * (x[95] + x[97]) +
    0.125f * x[96];
  memcpy(&x[FIR_SIZE - DECIMATE_FACTOR], x, DECIMATE_FACTOR * sizeof(float));
  return y;
}

void ayumi_process(struct ayumi* ay) {
  int i;
  float y1;
  float* c_left = ay->interpolator_left.c;
  float* y_left = ay->interpolator_left.y;
  float* fir_left = &ay->fir_left[FIR_SIZE - ay->fir_index * DECIMATE_FACTOR];
  ay->fir_index = (ay->fir_index + 1) % (FIR_SIZE / DECIMATE_FACTOR - 1);
  for (i = DECIMATE_FACTOR - 1; i >= 0; i -= 1) {
    ay->x += ay->step;
    if (ay->x >= 1.0f) {
      ay->x -= 1.0f;
      y_left[0] = y_left[1];
      y_left[1] = y_left[2];
      y_left[2] = y_left[3];
      update_mixer(ay);
      y_left[3] = ay->left;
      y1 = y_left[2] - y_left[0];
      c_left[0] = 0.5f * y_left[1] + 0.25f * (y_left[0] + y_left[2]);
      c_left[1] = 0.5f * y1;
      c_left[2] = 0.25f * (y_left[3] - y_left[1] - y1);
    }
    fir_left[i] = (c_left[2] * ay->x + c_left[1]) * ay->x + c_left[0];
  }
  float common = decimate(fir_left);
  ay->left = common;
  ay->right = common;
}

static float dc_filter(struct dc_filter* dc, int index, float x) {
  dc->sum += -dc->delay[index] + x;
  dc->delay[index] = x; 
  return x - dc->sum / (float)DC_FILTER_SIZE;
}

void ayumi_remove_dc(struct ayumi* ay) {
  float common = dc_filter(&ay->dc_left, ay->dc_index, ay->left);
  ay->left = common;
  ay->right = common;
  ay->dc_index = (ay->dc_index + 1) & (DC_FILTER_SIZE - 1);
}
