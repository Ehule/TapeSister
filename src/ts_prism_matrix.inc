/* Audio-clocked destination scheduling. All allocation, file I/O and UI work
   stay outside this module. Two frozen patches protect the current transition;
   cells contain only letters, read afresh when the next step starts. */
static float prism_timed_position(float start,float target,double elapsed,double duration)
{
    return lerp(start,target,fminf(1,(float)elapsed/(float)duration));
}
static void prism_matrix_step(TsPrism *p,int step)
{
    TsPrismMatrix *m=&p->matrix;const TsPrismControls *c=&p->controls;
    /* Every leg completes before another cell is visited. */
    if(m->position>=1) {m->pair[0]=m->pair[1];m->letter[0]=m->letter[1];}
    m->pair[1]=m->pair[0];m->letter[1]=m->letter[0];
    m->step=step;m->waiting=0;m->elapsed=0;m->position=m->start=0;m->target=1;
    m->duration=c->matrix.step_seconds;m->morph_duration=fmin(c->morph_seconds,m->duration);
    int state=c->matrix.step[step];
    m->missing=state>=0 && !(c->captured&(1<<state));
    if(state>=0 && !m->missing) {m->pair[1]=*ts_prism_state_const(c,state);m->letter[1]=state;}
}
static void prism_matrix_tick(TsPrism *p,double hop)
{
    TsPrismMatrix *m=&p->matrix;const TsPrismControls *c=&p->controls;
    if(m->pending) {
        m->pending=0;
        if(!m->active) {
            if(c->morph_enabled) {
                m->pair[0]=*ts_prism_endpoint_patch(c,0);m->pair[1]=*ts_prism_endpoint_patch(c,1);
                m->letter[0]=c->endpoint[0];m->letter[1]=c->endpoint[1];m->position=p->morph_position;
            } else {
                m->pair[0]=m->pair[1]=prism_patch(c);m->letter[0]=m->letter[1]=-1;m->position=0;
            }
        }
        m->active=m->running=1;m->step=-1;m->missing=0;
        if(m->position>0 && m->position<1) {
            m->waiting=1;m->start=m->position;m->target=m->position>=.5f?1:0;
            m->elapsed=0;m->duration=m->morph_duration=fmin(c->morph_seconds,c->matrix.step_seconds);
        } else prism_matrix_step(p,0);
    }
    if(!m->active || !m->running)return;
    if(m->elapsed>=m->duration) {
        double remainder=m->elapsed-m->duration;
        m->position=m->target;
        if(m->waiting)prism_matrix_step(p,0);
        else if(m->step+1<c->matrix.length)prism_matrix_step(p,m->step+1);
        else if(c->matrix.loop)prism_matrix_step(p,0);
        else {m->running=0;return;}
        m->elapsed=remainder;
    }
    m->position=prism_timed_position(m->start,m->target,m->elapsed,m->morph_duration);
    m->elapsed+=hop;
}
int ts_prism_matrix_export(const TsPrism *p,TsPrismControls *c)
{
    if(!p || !c || !p->matrix.active)return 0;
    for(int side=0;side<2;++side) {c->active_pair[side]=p->matrix.pair[side];c->endpoint[side]=p->matrix.letter[side];}
    c->active_pair_valid=1;c->morph_enabled=1;c->morph=p->matrix.position;
    c->morph_target=(int)p->matrix.target;c->morph_trigger=0;c->matrix_run=0;
    return 1;
}
