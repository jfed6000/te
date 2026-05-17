/* Legacy dcc-compatible sgstat.h shim for cmoc builds. */
struct sgbuf {
   char sg_class,
        sg_case,
        sg_backsp,
        sg_delete,
        sg_echo,
        sg_alf,
        sg_nulls,
        sg_pause,
        sg_page,
        sg_bspch,
        sg_dlnch,
        sg_eorch,
        sg_eofch,
        sg_rlnch,
        sg_dulnch,
        sg_psch,
        sg_kbich,
        sg_kbach,
        sg_bsech,
        sg_bellch,
        sg_parity,
        sg_baud;
   int  sg_d2p;
   char sg_xon,
        sg_xoff,
        sg_err;
   int  sg_tbl;
   char sg_spare[3];
};
