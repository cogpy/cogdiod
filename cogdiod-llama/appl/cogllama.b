implement CogLlama;

#
# cogllama.b — CogDiod ↔ llama.limbo Limbo wrapper
#
# Imports the llama.limbo modules (gguf, tokenizer, sampler, llama) and
# adapts them to the CogDiod message protocol so that an AtomIsolate of
# type "llm-coprocessor" running this Dis bytecode can serve LLM inference
# requests received as MSG_LLM_INFER messages on its incoming channel.
#
# Build: requires both repos to be installed in the Inferno tree:
#   /dis/lib/llama/{gguf,tokenizer,sampler,tensor,llama}.dis  (cogpy/llama.limbo)
#   /dis/lib/cogdiod/atom.dis                                 (cogpy/cogdiod)
#

include "sys.m";
    sys: Sys;

include "draw.m";

include "/dis/lib/llama/llama.m";
    llama: Llama;
    LModel: import llama;

include "/dis/lib/llama/sampler.m";
    sampler: Sampler;
    SamplerCfg: import sampler;

include "/dis/lib/cogdiod/atom.m";          # MSG_* constants and CogMessage adt
    atom: Atom;
    CogMessage, MSG_LLM_INFER, MSG_LLM_TOKEN, MSG_LLM_DONE: import atom;

CogLlama: module
{
    init: fn(ctxt: ref Draw->Context, argv: list of string);
};

# Per-instance state — bound to one AtomIsolate
state: ref LMState;
LMState: adt {
    model:   ref LModel;
    cfg:     ref SamplerCfg;
    pos:     int;        # current KV-cache position
};

init(_ctxt: ref Draw->Context, argv: list of string)
{
    sys     = load Sys     Sys->PATH;
    llama   = load Llama   Llama->PATH;
    sampler = load Sampler Sampler->PATH;
    atom    = load Atom    Atom->PATH;

    if (llama   == nil) die("cannot load llama");
    if (sampler == nil) die("cannot load sampler");
    if (atom    == nil) die("cannot load cogdiod atom");

    err := llama->init();   if (err != nil) die("llama->init: " + err);
    err  = sampler->init(); if (err != nil) die("sampler->init: " + err);

    if (len argv < 2)
        die("usage: cogllama <model.gguf> [-t temp] [-k topk] ...");

    model_path := hd tl argv;
    state = ref LMState;
    state.model = llama->openmodel(model_path);
    if (state.model == nil) die("openmodel failed: " + model_path);
    state.cfg   = sampler->newcfg();
    state.pos   = 0;

    # ---------------------------------------------------------------------
    # Main loop — receive CogMessages from the Atom's incoming channel,
    # dispatch LLM ops, and emit reply messages on the outgoing channel.
    # ---------------------------------------------------------------------
    in  := atom->incoming();
    out := atom->outgoing();

    for (;;) {
        msg := <- in;
        if (msg == nil) break;        # channel closed → atom destroyed
        case msg.mtype {
            MSG_LLM_INFER =>
                handle_infer(msg, out);
            * =>
                ;                     # ignore non-LLM messages
        }
    }
}

handle_infer(msg: ref CogMessage, out: chan of ref CogMessage)
{
    prompt := string msg.payload;
    # Stream tokens one at a time as MSG_LLM_TOKEN, then MSG_LLM_DONE
    chan_tokens := chan of ref CogMessage;
    spawn stream_tokens(prompt, out);
}

stream_tokens(prompt: string, out: chan of ref CogMessage)
{
    n := 0;
    for (tok := llama->stream(state.model, prompt, state.cfg, state.pos);
         tok != nil;
         tok  = llama->stream(state.model, nil,    state.cfg, state.pos)) {
        m := ref CogMessage(MSG_LLM_TOKEN, array of byte tok, big 0);
        out <- = m;
        state.pos++;
        n++;
        if (n >= state.cfg.max_tokens) break;
    }
    done := ref CogMessage(MSG_LLM_DONE, nil, big n);
    out <- = done;
}

die(s: string)
{
    sys->fprint(sys->fildes(2), "cogllama: %s\n", s);
    raise "fail:cogllama";
}
