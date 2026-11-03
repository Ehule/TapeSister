void ts_sister_filter_render(TsFramebuffer *fb,const TsCharacterFilterControls *c,
 float gain,float mix,int enabled,unsigned shared_locks,const TsPalette *palette)
{
 render_palette=palette;render_group=TS_THEME_PEDALBOARD;clear(fb,PAL_DESKTOP);
 sister_compact_button(fb,16,8,46,"ON",enabled);
 const char *routes[]={"LINKED","SPREAD","DUAL MONO"};
 int route=(int)lrintf(c->value[TS_CF_ROUTE]*2);
 for(int i=0;i<3;++i)sister_compact_button(fb,82+i*100,8,94,routes[i],route==i);
 for(int ch=0;ch<2;++ch){int left=18+ch*312,b=ch*8;uint32_t color=ch?PAL_WAVE_RIGHT:PAL_WAVE_LEFT;
  TsCharacterFilterControls shown=*c;
  if(ch && route!=TS_CF_DUAL){for(int i=TS_CF_L_MODEL;i<=TS_CF_L_TRIM;++i)shown.value[i+8]=c->value[i];color=sister_dim_color(color);}
  rect(fb,left-4,42,300,176,ts_theme_color(palette,render_group,TS_COLOR_BACKGROUND,RGB(10,10,11)));
  text(fb,left,47,ch?(route==TS_CF_DUAL?"RIGHT":"RIGHT / SHARED BASE"):"LEFT",color,1);
  if(route==TS_CF_DUAL && (c->value[TS_CF_FOCUS]>=.5f)==ch)rect(fb,left,59,42,1,color);
  int fields[7]={TS_CF_L_MODEL+b,TS_CF_L_HP+b,TS_CF_L_HP_RES+b,TS_CF_L_CUT+b,TS_CF_L_RES+b,TS_CF_L_DRIVE+b,TS_CF_L_TRIM+b};
  for(int k=0;k<7;++k){int field=fields[k],x=left,top=66,w=292;
   if(k){x+=((k-1)&1)*148;top=100+((k-1)/2)*40;w=140;}
   char s[40];
   if((k==1 || k==2) && shown.value[TS_CF_L_MODEL+b]>=.5f){sister_compact_button(fb,x,top,w,k==1?"LOW PASS":"BAND PASS",(shown.value[TS_CF_L_MODE+b]>=.5f)==(k==2));continue;}
   ts_character_filter_label(&shown,field,s,sizeof(s));
   sister_value_parameter_state(fb,x,top,w,s,shown.value[field],color,(c->locks&(1u<<field))!=0);
  }
  text(fb,left,207,shown.value[TS_CF_L_MODEL+b]>=.5f?"POLIVOKS / LP OR BP":"MS-20 / HP > LP",color,1);
 }
 int fields[]={TS_CF_SPREAD_AMOUNT,TS_CF_LFO_RATE,TS_CF_LFO_DEPTH,TS_CF_LFO_PHASE,TS_CF_ENV_AMOUNT,TS_CF_ENV_ATTACK,TS_CF_ENV_RELEASE};
 for(int k=0;k<7;++k){int x=k<3?18+k*204:k<5?18+(k-3)*204:k==5?426:526;int y=k<3?236:268,w=k>=5?88:190;char s[40];
  ts_character_filter_label(c,fields[k],s,sizeof(s));if(k==5)snprintf(s,sizeof(s),"ATK %.0F MS",powf(500,c->value[fields[k]]));if(k==6)snprintf(s,sizeof(s),"REL %.0F MS",10*powf(300,c->value[fields[k]]));
  sister_value_parameter_state(fb,x,y,w,s,c->value[fields[k]],PAL_TUNING,(c->locks&(1u<<fields[k]))!=0);
 }
 sister_percent_parameter_state(fb,18,304,292,"WET",mix,100,PAL_EFFECT,(shared_locks&16)!=0);
 sister_db_parameter_state(fb,324,304,292,"OUTPUT",gain,PAL_VOLUME,(shared_locks&1)!=0);
 sister_compact_button(fb,18,340,32,"<",0);sister_compact_button(fb,584,340,32,">",0);
 char label[64];snprintf(label,sizeof(label),"PRESET: %s",ts_character_filter_preset_name(c->preset));text(fb,68,346,label,PAL_MOUSE,1);
 for(int i=0;i<5;++i){snprintf(label,sizeof(label),"%c",'A'+i);sister_compact_button(fb,18+i*44,374,38,label,(c->captured&(1u<<i))!=0);}
 snprintf(label,sizeof(label),"SAVE %c",'A'+(c->selected?c->selected-1:0));sister_compact_button(fb,260,374,118,label,0);
 sister_compact_button(fb,560,374,60,"CLOSE",0);
 text(fb,388,379,"SHIFT: LOCK/SAVE",PAL_MOUSE,1);
}
