#ifndef STEMMER_H
#define STEMMER_H

#include "cstr.h"

static int _stm_is_vowel(char c) { return c=='a'||c=='e'||c=='i'||c=='o'||c=='u'; }

static int _stm_is_vowel_at(const char *w, int i) {
    if (_stm_is_vowel(w[i])) return 1;
    if (w[i] == 'y' && i > 0 && !_stm_is_vowel(w[i-1])) return 1;
    return 0;
}

static int _stm_measure(const char *w, int len) {
    int m = 0, i = 0;
    while (i < len && !_stm_is_vowel_at(w, i)) i++;
    while (i < len) {
        while (i < len && _stm_is_vowel_at(w, i)) i++;
        if (i >= len) break;
        m++;
        while (i < len && !_stm_is_vowel_at(w, i)) i++;
    }
    return m;
}

static int _stm_has_vowel(const char *w, int len) { for (int i=0;i<len;i++) if (_stm_is_vowel_at(w,i)) return 1; return 0; }

static int _stm_ends(const char *w, int len, const char *suf) {
    int sl = cstr_len(suf); if (sl > len) return 0;
    for (int i=0;i<sl;i++) if (w[len-sl+i]!=suf[i]) return 0;
    return 1;
}

static int _stm_double_cons(const char *w, int len) { if (len<2) return 0; return w[len-1]==w[len-2] && !_stm_is_vowel_at(w,len-1); }

static int _stm_cvc(const char *w, int len) {
    if (len<3) return 0;
    if (_stm_is_vowel_at(w,len-1)) return 0;
    if (!_stm_is_vowel_at(w,len-2)) return 0;
    if (_stm_is_vowel_at(w,len-3)) return 0;
    char c=w[len-1]; if (c=='w'||c=='x'||c=='y') return 0;
    return 1;
}

static void _stm_replace(char *w, int *len, int suflen, const char *rep) {
    int rlen=cstr_len(rep), base=*len-suflen;
    for (int i=0;i<rlen;i++) w[base+i]=rep[i];
    w[base+rlen]='\0'; *len=base+rlen;
}

typedef void (*_stm_step_fn)(char *w, int *len);

static void _stm_step1a(char *w, int *len) {
    if (_stm_ends(w,*len,"sses")) { _stm_replace(w,len,4,"ss"); return; }
    if (_stm_ends(w,*len,"ies"))  { _stm_replace(w,len,3,"i"); return; }
    if (_stm_ends(w,*len,"ss"))   return;
    if (_stm_ends(w,*len,"s") && *len>2) { (*len)--; w[*len]='\0'; }
}

static void _stm_step1b(char *w, int *len) {
    int flag=0;
    if (_stm_ends(w,*len,"eed")) { if (_stm_measure(w,*len-3)>0) _stm_replace(w,len,3,"ee"); return; }
    if (_stm_ends(w,*len,"ed")) { if (_stm_has_vowel(w,*len-2)) { _stm_replace(w,len,2,""); flag=1; } }
    else if (_stm_ends(w,*len,"ing")) { if (_stm_has_vowel(w,*len-3)) { _stm_replace(w,len,3,""); flag=1; } }
    if (flag) {
        if (_stm_ends(w,*len,"at")) _stm_replace(w,len,2,"ate");
        else if (_stm_ends(w,*len,"bl")) _stm_replace(w,len,2,"ble");
        else if (_stm_ends(w,*len,"iz")) _stm_replace(w,len,2,"ize");
        else if (_stm_double_cons(w,*len)) { char c=w[*len-1]; if (c!='l'&&c!='s'&&c!='z') { (*len)--; w[*len]='\0'; } }
        else if (_stm_measure(w,*len)==1 && _stm_cvc(w,*len)) { w[*len]='e'; (*len)++; w[*len]='\0'; }
    }
}

static void _stm_step1c(char *w, int *len) { if (*len>2 && w[*len-1]=='y' && _stm_has_vowel(w,*len-1)) w[*len-1]='i'; }

static void _stm_step2(char *w, int *len) {
    struct { const char *suf; int sl; const char *rep; } tab[] = {
        {"ational",7,"ate"},{"tional",6,"tion"},{"enci",4,"ence"},{"anci",4,"ance"},{"izer",4,"ize"},
        {"abli",4,"able"},{"alli",4,"al"},{"entli",5,"ent"},{"eli",3,"e"},{"ousli",5,"ous"},
        {"ization",7,"ize"},{"ation",5,"ate"},{"ator",4,"ate"},{"alism",5,"al"},{"iveness",7,"ive"},
        {"fulness",7,"ful"},{"ousness",7,"ous"},{"aliti",5,"al"},{"iviti",5,"ive"},{"biliti",6,"ble"},{NULL,0,NULL}
    };
    for (int i=0;tab[i].suf;i++) if (_stm_ends(w,*len,tab[i].suf)) { if (_stm_measure(w,*len-tab[i].sl)>0) _stm_replace(w,len,tab[i].sl,tab[i].rep); return; }
}

static void _stm_step3(char *w, int *len) {
    struct { const char *suf; int sl; const char *rep; } tab[] = {
        {"icate",5,"ic"},{"ative",5,""},{"alize",5,"al"},{"iciti",5,"ic"},{"ical",4,"ic"},{"ful",3,""},{"ness",4,""},{NULL,0,NULL}
    };
    for (int i=0;tab[i].suf;i++) if (_stm_ends(w,*len,tab[i].suf)) { if (_stm_measure(w,*len-tab[i].sl)>0) _stm_replace(w,len,tab[i].sl,tab[i].rep); return; }
}

static void _stm_step4(char *w, int *len) {
    const char *sx[]={"al","ance","ence","er","ic","able","ible","ant","ement","ment","ent","ion","ou","ism","ate","iti","ous","ive","ize",NULL};
    for (int i=0;sx[i];i++) {
        int sl=cstr_len(sx[i]);
        if (_stm_ends(w,*len,sx[i])) {
            int base=*len-sl;
            if (sx[i][0]=='i'&&sx[i][1]=='o'&&sx[i][2]=='n') { if (base>0&&(w[base-1]=='s'||w[base-1]=='t')&&_stm_measure(w,base)>1) { *len=base; w[*len]='\0'; } }
            else if (_stm_measure(w,base)>1) { *len=base; w[*len]='\0'; }
            return;
        }
    }
}

static void _stm_step5a(char *w, int *len) {
    if (w[*len-1]=='e') { int m=_stm_measure(w,*len-1); if (m>1||(m==1&&!_stm_cvc(w,*len-1))) { (*len)--; w[*len]='\0'; } }
}

static void _stm_step5b(char *w, int *len) {
    if (_stm_measure(w,*len)>1 && _stm_double_cons(w,*len) && w[*len-1]=='l') { (*len)--; w[*len]='\0'; }
}

static void stem_english(char *word) {
    int len = cstr_len(word);
    if (len <= 2) return;
    _stm_step_fn pipeline[] = { _stm_step1a, _stm_step1b, _stm_step1c, _stm_step2, _stm_step3, _stm_step4, _stm_step5a, _stm_step5b, NULL };
    for (int i = 0; pipeline[i]; i++) pipeline[i](word, &len);
}

#endif
