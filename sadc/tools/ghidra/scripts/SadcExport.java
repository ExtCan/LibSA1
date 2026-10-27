// SADC post-analysis: function-discovery fixups for SHC code, literal-pool typing,
// then export functions.json / xrefs.json / summary.json / decomp/<ENTRY>.c.
//
// Fixup passes (in this order; functions created here are tagged):
//   pools      define literal-pool data referenced by PC-relative MOV.L/MOV.W; when the
//              block is writable, mark it constant so the decompiler folds the value
//   longjumps  "mov.l @(disp,PC),Rn; jmp @Rn" without a preceding epilogue, to code that
//              uses the live stack frame, is an in-function long branch (SHC), not a tail
//              call: drop functions created at such targets, restore jump flow, re-grow body
//   switches   SHC "mova / mov.w @(R0,R1),R0 / braf R0" switch tables the decompiler
//              missed: case references, drop functions created at case targets, define the
//              table, re-grow the body, write a jump-table override
//   weakseeds  weak pointer seeds (gen_seeds.py --weak-out): only where no function/code
//              exists yet and the address starts a valid subroutine      -> SADC_WEAKSEED
//   orphans    disassembled code outside every function (e.g. targets of tail-call JMPs)
//              at a clean boundary                                       -> SADC_ORPHAN
//   gapfill    undefined gaps in executable blocks starting a valid subroutine
//              (PseudoDisassembler, must terminate) at a clean boundary  -> SADC_GAPFILL
//   (longjumps + switches run once more after gapfill, for code discovered by it)
//   tailcalls  JMP @Rn to another function's entry, or through a function pointer after the
//              epilogue / in a frameless function, gets flow override CALL_RETURN so the
//              decompiler shows "return f(...);" instead of "Could not recover jumptable"
// weakseeds/orphans/gapfill never start a function at code that uses an existing stack
// frame (case blocks, shared epilogues), nor right after a call's delay slot.
//
// Script args (key=value):
//   out=<dir>            output directory (required)
//   decomp=all|none|<S-E;S-E...>   functions (by entry) to decompile; default all
//   timeout=<sec>        per-function decompiler timeout (default 30)
//   threads=<n>          decompiler threads (default: Ghidra's pool size = #cores)
//   gapfill=0|1 pools=0|1 orphans=0|1 tailcalls=0|1 longjumps=0|1 switches=0|1 (all default 1)
//   weakseeds=<file>     weak seed file (same format as SadcSetup seeds)
//   xrefs=0|1            write xrefs.json (default 1)
//   decompout=files|single|both   decomp/<ENTRY>.c files and/or one decomp_all.c (default files)
//@category SADC

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.time.Instant;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;

import generic.concurrent.GThreadPool;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.decompiler.*;
import ghidra.app.decompiler.parallel.*;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.PseudoDisassembler;
import ghidra.program.model.address.*;
import ghidra.program.model.data.*;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import ghidra.util.task.TaskMonitor;

public class SadcExport extends GhidraScript {

	static final String GAP_TAG = "SADC_GAPFILL";
	static final String ORPHAN_TAG = "SADC_ORPHAN";
	static final String WEAK_TAG = "SADC_WEAKSEED";

	private final Map<String, String> args = new LinkedHashMap<>();
	private final Map<String, Object> summary = new LinkedHashMap<>();
	private final Map<String, Double> timing = new LinkedHashMap<>();
	private Memory mem;
	private Listing listing;
	private ReferenceManager refMgr;
	private AddressSetView exec;
	private File outDir;
	private PseudoDisassembler pd;

	private boolean on(String k) {
		return !"0".equals(args.get(k));
	}

	private long tick(String name, long t0) {
		long t = System.nanoTime();
		timing.merge(name, (t - t0) / 1e9, Double::sum);
		return t;
	}

	@Override
	protected void run() throws Exception {
		for (String a : getScriptArgs()) {
			int i = a.indexOf('=');
			if (i > 0) {
				args.put(a.substring(0, i).trim(), a.substring(i + 1).trim());
			}
		}
		println("SadcExport args: " + args);
		if (!args.containsKey("out")) {
			throw new IllegalArgumentException("out=<dir> is required");
		}
		outDir = new File(args.get("out"));
		outDir.mkdirs();

		mem = currentProgram.getMemory();
		listing = currentProgram.getListing();
		refMgr = currentProgram.getReferenceManager();
		exec = mem.getExecuteSet().intersect(mem.getLoadedAndInitializedAddressSet());
		pd = new PseudoDisassembler(currentProgram);
		pd.setRespectExecuteFlag(true);
		println("functions after auto-analysis: " + currentProgram.getFunctionManager().getFunctionCount());
		summary.put("functions_after_analysis", currentProgram.getFunctionManager().getFunctionCount());

		long t = System.nanoTime();
		int pools = 0, weak = 0, orph = 0, gap = 0, tails = 0;
		if (on("pools")) {
			pools = markPools();
			println("pool data items defined/marked: " + pools);
		}
		t = tick("pools_s", t);
		if (on("longjumps")) {
			longJumps();
			analyzeChanges(currentProgram);
		}
		t = tick("longjumps_s", t);
		if (on("switches")) {
			if (brafSwitches() > 0) {
				analyzeChanges(currentProgram);
			}
		}
		t = tick("switches_s", t);
		if (args.containsKey("weakseeds") && !args.get("weakseeds").isEmpty()) {
			weak = weakSeeds(new File(args.get("weakseeds")));
			if (weak > 0) {
				analyzeChanges(currentProgram);
			}
		}
		t = tick("weakseeds_s", t);
		if (on("orphans")) {
			orph += orphans();
		}
		t = tick("orphans_s", t);
		if (on("gapfill")) {
			for (int pass = 1; pass <= 12; pass++) {
				int made = gapFillPass(pass);
				gap += made;
				if (made == 0) {
					break;
				}
				analyzeChanges(currentProgram);
				if (on("pools")) {
					pools += markPools();
				}
				if (on("orphans")) {
					orph += orphans();
				}
			}
		}
		t = tick("gapfill_s", t);
		// code found by gapfill/orphans can contain more long branches and switches whose
		// targets gapfill may have claimed; clean those up again before tail-call marking
		if (gap + orph + weak > 0) {
			if (on("longjumps")) {
				longJumps();
			}
			if (on("switches")) {
				brafSwitches();
			}
			analyzeChanges(currentProgram);
		}
		t = tick("cleanup_s", t);
		if (on("tailcalls")) {
			tails = tailCalls();
			if (tails > 0) {
				analyzeChanges(currentProgram);
			}
		}
		t = tick("tailcalls_s", t);
		summary.put("pool_items", pools);
		summary.put("weakseed_functions", weak);
		summary.put("orphan_functions", orph);
		summary.put("gapfill_functions", gap);
		summary.put("tailcall_overrides", tails);

		List<Function> funcs = new ArrayList<>();
		for (Function f : listing.getFunctions(true)) {
			funcs.add(f);
		}
		println("functions: " + funcs.size());

		writeFunctionsJson(funcs, Collections.emptyMap(), Collections.emptyMap());
		if (on("xrefs")) {
			writeXrefsJson(funcs);
		}
		t = tick("json_s", t);

		Map<Address, String> status = new ConcurrentHashMap<>();
		Map<Address, Long> ms = new ConcurrentHashMap<>();
		String dsel = args.getOrDefault("decomp", "all");
		if (!"none".equals(dsel)) {
			decompileAll(funcs, dsel, status, ms);
		}
		t = tick("decomp_s", t);

		writeFunctionsJson(funcs, status, ms);
		writeSummary(funcs, status);
		println("SadcExport done: " + outDir);
	}

	// ------------------------------------------------------------------ helpers

	static String hx(Address a) {
		return String.format("0x%08X", a.getOffset());
	}

	static String hx(long v) {
		return String.format("0x%08X", v);
	}

	static String js(String s) {
		if (s == null) {
			return "null";
		}
		StringBuilder b = new StringBuilder("\"");
		for (char c : s.toCharArray()) {
			switch (c) {
				case '"': b.append("\\\""); break;
				case '\\': b.append("\\\\"); break;
				case '\n': b.append("\\n"); break;
				case '\r': b.append("\\r"); break;
				case '\t': b.append("\\t"); break;
				default:
					if (c < 0x20) {
						b.append(String.format("\\u%04x", (int) c));
					}
					else {
						b.append(c);
					}
			}
		}
		return b.append('"').toString();
	}

	private void write(File f, String s) throws IOException {
		Files.writeString(f.toPath(), s, StandardCharsets.UTF_8);
	}

	private String blockName(Address a) {
		MemoryBlock b = mem.getBlock(a);
		return b == null ? null : b.getName();
	}

	private boolean isPad(int w) {
		return w == 0x0000 || w == 0x0009 || w == 0x0900;
	}

	/** Mnemonics after whose delay slot execution never continues (SH-4). */
	private static final Set<String> NO_FALLTHROUGH = Set.of("rts", "rte", "bra", "braf", "jmp");

	/**
	 * True if the code unit before 'a' cannot fall through into 'a'. Decided from the
	 * mnemonic, not the flow type: a call to a function wrongly marked non-returning has
	 * flow CALL_TERMINATOR but execution does continue after its delay slot.
	 */
	private boolean cleanBoundary(Address a) {
		CodeUnit cu = listing.getCodeUnitBefore(a);
		if (cu == null || !(cu instanceof Instruction)) {
			return true; // data / undefined / start of block
		}
		if (cu.getMaxAddress().add(1).compareTo(a) != 0) {
			return true;
		}
		Instruction ins = (Instruction) cu;
		if (ins.isInDelaySlot()) {
			Instruction br = listing.getInstructionBefore(ins.getAddress());
			return br != null && NO_FALLTHROUGH.contains(br.getMnemonicString().toLowerCase());
		}
		return NO_FALLTHROUGH.contains(ins.getMnemonicString().toLowerCase());
	}

	private int flowRefCount(Instruction j) {
		int n = 0;
		for (Reference r : j.getReferencesFrom()) {
			if (r.getReferenceType().isFlow()) {
				n++;
			}
		}
		return n;
	}

	// ------------------------------------------------------------------ literal pools

	/** Define data for PC-relative MOV.L/MOV.W pool entries; mark constant when writable. */
	private int markPools() throws Exception {
		int n = 0;
		DataType dw = DWordDataType.dataType;
		DataType w16 = WordDataType.dataType;
		DataType ptr = PointerDataType.dataType;
		InstructionIterator it = listing.getInstructions(exec, true);
		while (it.hasNext()) {
			monitor.checkCancelled();
			Instruction ins = it.next();
			Address a = ins.getAddress();
			int w;
			try {
				w = mem.getShort(a) & 0xffff;
			}
			catch (MemoryAccessException e) {
				continue;
			}
			int op = w >>> 12;
			if (op != 0xD && op != 0x9) {
				continue;
			}
			int size = op == 0xD ? 4 : 2;
			long t = op == 0xD ? ((a.getOffset() & ~3L) + (w & 0xff) * 4L + 4)
					: (a.getOffset() + (w & 0xff) * 2L + 4);
			Address ta = a.getNewAddress(t);
			if (!mem.contains(ta) || !mem.contains(ta.add(size - 1))) {
				continue;
			}
			Data d = listing.getDefinedDataAt(ta);
			if (d == null) {
				if (!listing.isUndefined(ta, ta.add(size - 1))) {
					continue; // overlaps an instruction or other data
				}
				DataType dt = w16;
				if (size == 4) {
					dt = dw;
					try {
						long v = mem.getInt(ta) & 0xffffffffL;
						if (v != 0 && mem.contains(ta.getNewAddress(v))) {
							dt = ptr;
						}
					}
					catch (Exception e) {
						// keep dword
					}
				}
				try {
					d = listing.createData(ta, dt);
					n++;
				}
				catch (Exception e) {
					continue;
				}
			}
			MemoryBlock b = mem.getBlock(ta);
			if (d != null && b != null && b.isWrite() && d.getLength() == size &&
				MutabilitySettingsDefinition.DEF.getChoice(d) != MutabilitySettingsDefinition.CONSTANT) {
				MutabilitySettingsDefinition.DEF.setChoice(d, MutabilitySettingsDefinition.CONSTANT);
				n++;
			}
		}
		return n;
	}

	// ------------------------------------------------------------------ SHC long branches

	private int word(Address a) {
		try {
			return mem.getShort(a) & 0xffff;
		}
		catch (Exception e) {
			return -1;
		}
	}

	/** Epilogue-only: LDS.L @R15+,PR/MACL, MOV.L @R15+,Rn, FMOV.S @R15+,FRn, ADD #+imm,R15. */
	private static boolean isPop(int w) {
		return w == 0x4F26 || w == 0x4F16 || (w & 0xF0FF) == 0x60F6 || (w & 0xF0FF) == 0xF0F9 ||
			((w & 0xFF00) == 0x7F00 && (w & 0xFF) > 0 && (w & 0xFF) < 0x80);
	}

	/** Prologue: MOV.L Rm,@-R15, STS.L PR/MACL,@-R15, FMOV.S FRm,@-R15, ADD #-imm,R15. */
	private static boolean isPush(int w) {
		return w >= 0 && ((w & 0xFF0F) == 0x2F06 || w == 0x4F22 || w == 0x4F12 ||
			(w & 0xFF0F) == 0xFF0B || ((w & 0xFF00) == 0x7F00 && (w & 0x80) != 0));
	}

	/** Code at t touches an existing stack frame before setting one up (not a function entry). */
	private boolean usesFrame(Address t) {
		Address a = t;
		for (int k = 0; k < 12; k++) {
			int w = word(a);
			if (w < 0 || isPush(w)) {
				return false;
			}
			if (isPop(w) || (w & 0xF0F0) == 0x50F0 || (w & 0xF0FF) == 0x60F2 ||
				(w & 0xFF00) == 0x1F00 || (w & 0xFFF0) == 0x85F0 || (w & 0xFFF0) == 0x84F0 ||
				(w & 0xF0FF) == 0xF0F6 || (w & 0xF0FF) == 0xF0F7) {
				return true;
			}
			if (w == 0x000B || (w & 0xF000) == 0xA000 || (w & 0xF0FF) == 0x402B ||
				(w & 0xF0FF) == 0x0023) {
				return false; // rts / bra / jmp / braf
			}
			a = a.add(2);
		}
		return false;
	}

	private boolean epilogueBefore(Address j) {
		if (isPop(word(j.add(2)))) {
			return true; // delay slot
		}
		Address a = j;
		for (int k = 0; k < 12; k++) {
			a = a.subtract(2);
			int w = word(a);
			if (w < 0 || w == 0x000B) {
				return false;
			}
			if (isPop(w)) {
				return true;
			}
		}
		return false;
	}

	/** Constant target of JMP @Rn: its single flow reference, or MOV.L @(disp,PC),Rn before it. */
	private Address jmpTarget(Instruction j) {
		Address t = null;
		int n = 0;
		for (Reference r : j.getReferencesFrom()) {
			if (r.getReferenceType().isFlow()) {
				t = r.getToAddress();
				n++;
			}
		}
		if (n == 1) {
			return t;
		}
		if (n > 1) {
			return null;
		}
		int rn = (word(j.getAddress()) >> 8) & 15;
		Address a = j.getAddress();
		for (int k = 0; k < 8; k++) {
			a = a.subtract(2);
			int y = word(a);
			if (y < 0) {
				return null;
			}
			if ((y >> 12) == 0xD && ((y >> 8) & 15) == rn) {
				long po = (a.getOffset() & ~3L) + (y & 0xff) * 4L + 4;
				try {
					long v = mem.getInt(a.getNewAddress(po)) & 0xffffffffL;
					return a.getNewAddress(v);
				}
				catch (Exception e) {
					return null;
				}
			}
			if (y == 0x000B || (y & 0xF000) == 0xA000 || (y & 0xF0FF) == 0x402B ||
				(y & 0xF0FF) == 0x400B || (y & 0xF000) == 0xB000) {
				return null;
			}
		}
		return null;
	}

	/** Referenced by JSR/BSR/BSRF or from non-executable memory (a real function). */
	private boolean hasRealCallers(Address t) {
		for (Reference r : refMgr.getReferencesTo(t)) {
			Address from = r.getFromAddress();
			if (!from.isMemoryAddress()) {
				continue;
			}
			if (!exec.contains(from)) {
				return true;
			}
			Instruction i = listing.getInstructionAt(from);
			if (i != null) {
				String m = i.getMnemonicString().toLowerCase();
				if (m.equals("jsr") || m.equals("bsr") || m.equals("bsrf")) {
					return true;
				}
			}
		}
		return false;
	}

	/**
	 * SHC branches farther than BRA range inside one function with
	 * "mov.l @(disp,PC),Rn; jmp @Rn" (tail calls use the same idiom after the epilogue).
	 * For a JMP without a preceding epilogue whose target uses the live stack frame:
	 * remove any function Ghidra/seeding created at the target (unless it has JSR/BSR or
	 * data references), restore plain jump flow, and re-grow the enclosing function body.
	 */
	private int longJumps() throws Exception {
		List<Address[]> intra = new ArrayList<>();
		InstructionIterator it = listing.getInstructions(exec, true);
		while (it.hasNext()) {
			monitor.checkCancelled();
			Instruction j = it.next();
			if (!"jmp".equalsIgnoreCase(j.getMnemonicString())) {
				continue;
			}
			Address t = jmpTarget(j);
			if (t == null || !exec.contains(t) || epilogueBefore(j.getAddress()) || !usesFrame(t)) {
				continue;
			}
			intra.add(new Address[] { j.getAddress(), t });
		}
		Set<Address> targets = new HashSet<>();
		for (Address[] p : intra) {
			targets.add(p[1]);
		}
		int removed = 0, kept = 0, overrides = 0;
		for (Address t : targets) {
			Function ft = listing.getFunctionAt(t);
			if (ft == null) {
				continue;
			}
			if (hasRealCallers(t)) {
				kept++;
				continue;
			}
			currentProgram.getFunctionManager().removeFunction(t);
			removed++;
		}
		Set<Function> toFix = new HashSet<>();
		for (Address[] p : intra) {
			Instruction j = listing.getInstructionAt(p[0]);
			if (j == null) {
				continue;
			}
			if (j.getFlowOverride() != FlowOverride.NONE) {
				j.setFlowOverride(FlowOverride.NONE);
				overrides++;
			}
			boolean hasRef = false;
			for (Reference r : j.getReferencesFrom()) {
				if (r.getReferenceType().isFlow() && r.getToAddress().equals(p[1])) {
					hasRef = true;
				}
			}
			if (!hasRef) {
				refMgr.addMemoryReference(p[0], p[1], RefType.COMPUTED_JUMP, SourceType.ANALYSIS,
					CodeUnit.MNEMONIC);
			}
			if (listing.getInstructionAt(p[1]) == null && listing.isUndefined(p[1], p[1].add(1))) {
				disassemble(p[1]);
			}
			Function src = listing.getFunctionContaining(p[0]);
			if (src != null) {
				toFix.add(src);
			}
		}
		int fixed = 0;
		for (Function f : toFix) {
			if (CreateFunctionCmd.fixupFunctionBody(currentProgram, f, monitor)) {
				fixed++;
			}
		}
		println(String.format("long jumps: %d in-function JMPs (%d targets), %d target functions removed, " +
			"%d kept (have callers), %d flow overrides cleared, %d bodies re-grown",
			intra.size(), targets.size(), removed, kept, overrides, fixed));
		summary.put("long_jumps", intra.size());
		summary.merge("long_jump_functions_removed", removed, (x, y) -> (Integer) x + (Integer) y);
		return removed;
	}

	// ------------------------------------------------------------------ SHC switch tables

	/**
	 * SHC switch: "mov #N,Rx; cmp/hs Rx,R0; bf/bt ..; shll R0; mov R0,R1;
	 * mova @(d,PC),R0; mov.w @(R0,R1),R0; braf R0; nop" with a table of N signed 16-bit
	 * offsets relative to braf+4. Resolve tables the decompiler's switch analysis missed:
	 * drop functions created at case targets, add COMPUTED_JUMP references, define the
	 * table as word[N], re-grow the body and write a jump-table override for the decompiler.
	 */
	private int brafSwitches() throws Exception {
		List<Instruction> brafs = new ArrayList<>();
		InstructionIterator it = listing.getInstructions(exec, true);
		while (it.hasNext()) {
			Instruction i = it.next();
			if ("braf".equalsIgnoreCase(i.getMnemonicString())) {
				brafs.add(i);
			}
		}
		int resolved = 0, removed = 0, already = 0, unmatched = 0;
		Map<Address, List<Address>> tables = new LinkedHashMap<>();
		Set<Function> toFix = new HashSet<>();
		for (Instruction br : brafs) {
			monitor.checkCancelled();
			Address a = br.getAddress();
			Address mv = null;
			for (int k = 1; k <= 3; k++) {
				if ((word(a.subtract(2 * k)) & 0xFF00) == 0xC700) {
					mv = a.subtract(2 * k);
					break;
				}
			}
			int n = -1;
			for (int k = 1; k <= 16 && mv != null; k++) {
				Address b = a.subtract(2 * k);
				int x = word(b);
				if ((x & 0xFF0F) == 0x3002) {
					int boundReg = (x >> 4) & 15;
					for (int q = 1; q <= 6; q++) {
						int y = word(b.subtract(2 * q));
						if ((y >> 12) == 0xE && ((y >> 8) & 15) == boundReg) {
							n = y & 0xff;
							break;
						}
					}
					break;
				}
			}
			Function own = listing.getFunctionContaining(a);
			if (mv == null || n <= 0 || own == null) {
				unmatched++;
				continue;
			}
			long tbl = (mv.getOffset() & ~3L) + (word(mv) & 0xff) * 4L + 4;
			List<Address> targets = new ArrayList<>();
			boolean ok = true;
			for (int i = 0; i < n && ok; i++) {
				int e = word(a.getNewAddress(tbl + 2L * i));
				long t = a.getOffset() + 4 + (short) e;
				Address ta = a.getNewAddress(t);
				ok = e >= 0 && (t & 1) == 0 && exec.contains(ta);
				targets.add(ta);
			}
			if (!ok) {
				unmatched++;
				continue;
			}
			boolean allIn = true;
			for (Address t : targets) {
				allIn &= own.getBody().contains(t);
			}
			if (allIn && flowRefCount(br) > 1) {
				already++;
				continue;
			}
			for (Address t : new LinkedHashSet<>(targets)) {
				Function f = listing.getFunctionAt(t);
				if (f != null && !f.equals(own) && !hasRealCallers(t)) {
					currentProgram.getFunctionManager().removeFunction(t);
					removed++;
				}
			}
			Address ta = a.getNewAddress(tbl);
			if (listing.isUndefined(ta, ta.add(2L * n - 1))) {
				try {
					listing.createData(ta, new ArrayDataType(WordDataType.dataType, n, 2));
				}
				catch (Exception e) {
					// leave undefined
				}
			}
			for (Address t : new LinkedHashSet<>(targets)) {
				refMgr.addMemoryReference(a, t, RefType.COMPUTED_JUMP, SourceType.ANALYSIS,
					CodeUnit.MNEMONIC);
				if (listing.getInstructionAt(t) == null && listing.isUndefined(t, t.add(1))) {
					disassemble(t);
				}
			}
			tables.put(a, new ArrayList<>(new LinkedHashSet<>(targets)));
			toFix.add(own);
			resolved++;
		}
		for (Function f : toFix) {
			CreateFunctionCmd.fixupFunctionBody(currentProgram, f, monitor);
		}
		int overrides = 0;
		for (Map.Entry<Address, List<Address>> e : tables.entrySet()) {
			Function f = listing.getFunctionContaining(e.getKey());
			if (f == null) {
				continue;
			}
			try {
				new ghidra.program.model.pcode.JumpTable(e.getKey(),
					new ArrayList<>(e.getValue()), true, 0).writeOverride(f);
				overrides++;
			}
			catch (Exception ex) {
				println("jumptable override failed at " + e.getKey() + ": " + ex.getMessage());
			}
		}
		println(String.format("switch tables: %d BRAF, %d already resolved, %d resolved here " +
			"(%d case functions removed, %d overrides), %d not matching the SHC pattern",
			brafs.size(), already, resolved, removed, overrides, unmatched));
		summary.merge("switch_tables_resolved", resolved, (x, y) -> (Integer) x + (Integer) y);
		summary.merge("switch_case_functions_removed", removed, (x, y) -> (Integer) x + (Integer) y);
		return resolved;
	}

	// ------------------------------------------------------------------ weak seeds

	private int weakSeeds(File f) throws Exception {
		if (!f.isFile()) {
			printerr("weak seed file not found: " + f);
			return 0;
		}
		int made = 0, seen = 0;
		for (String line : Files.readAllLines(f.toPath(), StandardCharsets.UTF_8)) {
			int c = line.indexOf('#');
			if (c >= 0) {
				line = line.substring(0, c);
			}
			line = line.trim();
			if (line.isEmpty()) {
				continue;
			}
			String tok = line.split("\\s+")[0].replaceFirst("^0[xX]", "");
			Address a;
			try {
				a = toAddr(Long.parseUnsignedLong(tok, 16));
			}
			catch (Exception e) {
				continue;
			}
			seen++;
			if ((a.getOffset() & 1) != 0 || !exec.contains(a) ||
				listing.getFunctionContaining(a) != null || usesFrame(a)) {
				continue;
			}
			if (listing.getInstructionAt(a) == null) {
				if (!listing.isUndefined(a, a.add(1)) || !pd.isValidSubroutine(a, false, true) ||
					!disassemble(a)) {
					continue;
				}
			}
			Function fn = createFunction(a, null);
			if (fn != null) {
				fn.addTag(WEAK_TAG);
				made++;
			}
		}
		println("weak seeds: " + seen + " read, " + made + " functions created");
		return made;
	}

	// ------------------------------------------------------------------ orphan code

	private int orphans() throws Exception {
		List<Address> starts = new ArrayList<>();
		InstructionIterator it = listing.getInstructions(exec, true);
		Address prevEnd = null;
		boolean prevOrphan = false;
		while (it.hasNext()) {
			monitor.checkCancelled();
			Instruction ins = it.next();
			boolean orphan = listing.getFunctionContaining(ins.getAddress()) == null;
			if (orphan) {
				boolean contiguous = prevOrphan && prevEnd != null &&
					prevEnd.add(1).equals(ins.getAddress());
				if (!contiguous && !ins.isInDelaySlot()) {
					starts.add(ins.getAddress());
				}
			}
			prevOrphan = orphan;
			prevEnd = ins.getMaxAddress();
		}
		int made = 0;
		Set<Function> toFix = new HashSet<>();
		for (Address a : starts) {
			monitor.checkCancelled();
			if (listing.getFunctionContaining(a) != null) {
				continue;
			}
			boolean inFunc = false;
			for (Reference r : refMgr.getReferencesTo(a)) {
				RefType rt = r.getReferenceType();
				if (!rt.isFlow() || rt.isCall()) {
					continue;
				}
				Function src = listing.getFunctionContaining(r.getFromAddress());
				Instruction j = listing.getInstructionAt(r.getFromAddress());
				if (src == null || j == null) {
					continue;
				}
				if (rt.isComputed() && flowRefCount(j) == 1) {
					continue; // JMP @Rn with a single constant target: tail call
				}
				inFunc = true; // branch / switch target that the body missed
				toFix.add(src);
			}
			if (inFunc || !cleanBoundary(a) || usesFrame(a)) {
				continue;
			}
			Function fn = createFunction(a, null);
			if (fn != null) {
				fn.addTag(ORPHAN_TAG);
				made++;
			}
		}
		for (Function f : toFix) {
			CreateFunctionCmd.fixupFunctionBody(currentProgram, f, monitor);
		}
		println("orphans: " + starts.size() + " orphan runs, " + made + " functions created, " +
			toFix.size() + " function bodies fixed up");
		return made;
	}

	// ------------------------------------------------------------------ gap fill

	/** First non-padding even address after 'end' if it is undefined and executable. */
	private Address nextAfterPadding(Address end) {
		try {
			Address a = end.add(1);
			if ((a.getOffset() & 1) != 0) {
				a = a.add(1);
			}
			for (int i = 0; i < 64 && exec.contains(a); i++) {
				if (!listing.isUndefined(a, a.add(1))) {
					return null;
				}
				if (!isPad(mem.getShort(a) & 0xffff)) {
					return refMgr.hasReferencesTo(a) ? null : a;
				}
				a = a.add(2);
			}
		}
		catch (Exception e) {
			// end of memory
		}
		return null;
	}

	private int gapFillPass(int pass) throws Exception {
		AddressSetView undef = listing.getUndefinedRanges(exec, true, monitor);
		List<Address> cands = new ArrayList<>();
		for (AddressRange r : undef) {
			Address a = r.getMinAddress();
			if ((a.getOffset() & 1) != 0) {
				if (a.equals(r.getMaxAddress())) {
					continue;
				}
				a = a.add(1);
			}
			boolean prevPad = true;
			while (a.compareTo(r.getMaxAddress()) < 0) {
				int w = mem.getShort(a) & 0xffff;
				boolean pad = isPad(w);
				if (!pad && prevPad && !refMgr.hasReferencesTo(a)) {
					cands.add(a);
				}
				prevPad = pad;
				if (a.getOffset() + 2 > r.getMaxAddress().getOffset()) {
					break;
				}
				a = a.add(2);
			}
		}
		int made = 0;
		Deque<Address> work = new ArrayDeque<>(cands);
		while (!work.isEmpty()) {
			Address c = work.pollFirst();
			monitor.checkCancelled();
			if (!listing.isUndefined(c, c.add(1)) || !cleanBoundary(c) || usesFrame(c)) {
				continue;
			}
			if (!pd.isValidSubroutine(c, false, true)) {
				continue;
			}
			if (!disassemble(c)) {
				continue;
			}
			Function f = listing.getFunctionAt(c);
			if (f == null) {
				f = createFunction(c, null);
			}
			if (f != null) {
				f.addTag(GAP_TAG);
				made++;
				Address nx = nextAfterPadding(f.getBody().getMaxAddress());
				if (nx != null) {
					work.addFirst(nx);
				}
			}
		}
		println("gapfill pass " + pass + ": candidates=" + cands.size() + " created=" + made);
		return made;
	}

	// ------------------------------------------------------------------ tail calls

	private int tailCalls() throws Exception {
		int n = 0;
		Set<Function> fix = new HashSet<>();
		InstructionIterator it = listing.getInstructions(exec, true);
		while (it.hasNext()) {
			monitor.checkCancelled();
			Instruction j = it.next();
			FlowType ft = j.getFlowType();
			if (!ft.isJump() || !ft.isComputed() || ft.isConditional() ||
				j.getFlowOverride() != FlowOverride.NONE) {
				continue;
			}
			Address[] flows = j.getFlows();
			if ((flows == null || flows.length == 0) &&
				"jmp".equalsIgnoreCase(j.getMnemonicString())) {
				// JMP @Rn through a function pointer: a tail call if the frame is already
				// torn down, or if the function never set one up and the register does
				// not come from an indexed table load
				Function own = listing.getFunctionContaining(j.getAddress());
				boolean tail = epilogueBefore(j.getAddress());
				if (!tail && own != null && !isPush(word(own.getEntryPoint()))) {
					tail = true;
					Address b = j.getAddress();
					for (int k = 0; k < 6; k++) {
						b = b.subtract(2);
						int x = word(b);
						if ((x & 0xF00F) == 0x000E || (x & 0xF0FF) == 0x0023 ||
							(x & 0xFF00) == 0xC700) {
							tail = false;
						}
					}
				}
				if (tail) {
					j.setFlowOverride(FlowOverride.CALL_RETURN);
					if (own != null) {
						fix.add(own);
					}
					n++;
				}
				continue;
			}
			if (flows == null || flows.length != 1) {
				continue;
			}
			Function tf = listing.getFunctionAt(flows[0]);
			Function own = listing.getFunctionContaining(j.getAddress());
			if (tf == null || own == null || tf.equals(own)) {
				continue;
			}
			j.setFlowOverride(FlowOverride.CALL_RETURN);
			fix.add(own);
			n++;
		}
		for (Function f : fix) {
			CreateFunctionCmd.fixupFunctionBody(currentProgram, f, monitor);
		}
		println("tail calls: " + n + " JMP instructions overridden to CALL_RETURN (constant or indirect)");
		return n;
	}

	// ------------------------------------------------------------------ JSON

	private void writeFunctionsJson(List<Function> funcs, Map<Address, String> status,
			Map<Address, Long> ms) throws Exception {
		StringBuilder sb = new StringBuilder(funcs.size() * 400);
		sb.append("[\n");
		boolean first = true;
		for (Function f : funcs) {
			monitor.checkCancelled();
			AddressSetView body = f.getBody();
			Address entry = f.getEntryPoint();
			if (!first) {
				sb.append(",\n");
			}
			first = false;
			sb.append("{\"name\":").append(js(f.getName()));
			sb.append(",\"entry\":\"").append(hx(entry)).append('"');
			sb.append(",\"end\":\"").append(hx(body.getMaxAddress().getOffset() + 1)).append('"');
			sb.append(",\"size\":").append(body.getNumAddresses());
			sb.append(",\"ranges\":[");
			boolean f1 = true;
			for (AddressRange r : body) {
				if (!f1) {
					sb.append(',');
				}
				f1 = false;
				sb.append("[\"").append(hx(r.getMinAddress())).append("\",\"")
						.append(hx(r.getMaxAddress().getOffset() + 1)).append("\"]");
			}
			sb.append("]");
			sb.append(",\"block\":").append(js(blockName(entry)));
			List<String> tags = new ArrayList<>();
			for (FunctionTag t : f.getTags()) {
				tags.add(js(t.getName()));
			}
			Collections.sort(tags);
			sb.append(",\"tags\":[").append(String.join(",", tags)).append("]");
			sb.append(",\"thunk\":").append(f.isThunk());
			sb.append(",\"noreturn\":").append(f.hasNoReturn());
			sb.append(",\"signature\":").append(js(f.getPrototypeString(false, false)));
			sb.append(",\"calls\":[").append(entryList(f.getCalledFunctions(TaskMonitor.DUMMY)))
					.append("]");
			sb.append(",\"called_by\":[")
					.append(entryList(f.getCallingFunctions(TaskMonitor.DUMMY))).append("]");
			String st = status.get(entry);
			sb.append(",\"decomp\":").append(js(st == null ? "skipped" : st));
			Long t = ms.get(entry);
			if (t != null) {
				sb.append(",\"decomp_ms\":").append(t);
			}
			sb.append("}");
		}
		sb.append("\n]\n");
		write(new File(outDir, "functions.json"), sb.toString());
	}

	private String entryList(Set<Function> fs) {
		List<Long> v = new ArrayList<>();
		for (Function g : fs) {
			v.add(g.getEntryPoint().getOffset());
		}
		Collections.sort(v);
		StringBuilder b = new StringBuilder();
		for (Long x : v) {
			if (b.length() > 0) {
				b.append(',');
			}
			b.append('"').append(hx(x)).append('"');
		}
		return b.toString();
	}

	private static String hexList(Collection<Long> s) {
		StringBuilder b = new StringBuilder();
		for (Long x : new TreeSet<>(s)) {
			if (b.length() > 0) {
				b.append(',');
			}
			b.append('"').append(hx(x)).append('"');
		}
		return b.toString();
	}

	/**
	 * xrefs.json: per function, global data read/written (refs from instructions to
	 * non-executable memory) and the address-like values loaded from literal pools.
	 */
	private void writeXrefsJson(List<Function> funcs) throws Exception {
		StringBuilder sb = new StringBuilder(funcs.size() * 200);
		sb.append("{\n");
		boolean first = true;
		for (Function f : funcs) {
			monitor.checkCancelled();
			Set<Long> reads = new HashSet<>(), writes = new HashSet<>(), pool = new HashSet<>();
			InstructionIterator it = listing.getInstructions(f.getBody(), true);
			while (it.hasNext()) {
				Instruction ins = it.next();
				for (Reference r : ins.getReferencesFrom()) {
					Address to = r.getToAddress();
					if (!to.isMemoryAddress() || r.getReferenceType().isFlow()) {
						continue;
					}
					MemoryBlock b = mem.getBlock(to);
					if (b == null) {
						continue;
					}
					if (b.isExecute()) {
						int w = mem.getShort(ins.getAddress()) & 0xffff;
						if ((w >>> 12) == 0xD) {
							try {
								long v = mem.getInt(to) & 0xffffffffL;
								if (v != 0 && mem.contains(to.getNewAddress(v))) {
									pool.add(v);
								}
							}
							catch (Exception e) {
								// ignore
							}
						}
						continue;
					}
					RefType rt = r.getReferenceType();
					if (rt.isWrite()) {
						writes.add(to.getOffset());
					}
					if (rt.isRead() || !rt.isWrite()) {
						reads.add(to.getOffset());
					}
				}
			}
			if (!first) {
				sb.append(",\n");
			}
			first = false;
			sb.append('"').append(hx(f.getEntryPoint())).append("\":{\"reads\":[")
					.append(hexList(reads)).append("],\"writes\":[").append(hexList(writes))
					.append("],\"pool\":[").append(hexList(pool)).append("]}");
		}
		sb.append("\n}\n");
		write(new File(outDir, "xrefs.json"), sb.toString());
	}

	private void writeSummary(List<Function> funcs, Map<Address, String> status)
			throws Exception {
		StringBuilder sb = new StringBuilder();
		sb.append("{\n");
		sb.append("\"program\":").append(js(currentProgram.getName())).append(",\n");
		sb.append("\"executable_path\":").append(js(currentProgram.getExecutablePath()))
				.append(",\n");
		sb.append("\"language\":")
				.append(js(currentProgram.getLanguageID().getIdAsString())).append(",\n");
		sb.append("\"compiler_spec\":")
				.append(js(currentProgram.getCompilerSpec().getCompilerSpecID().getIdAsString()))
				.append(",\n");
		sb.append("\"generated\":").append(js(Instant.now().toString())).append(",\n");
		sb.append("\"function_count\":").append(funcs.size()).append(",\n");

		sb.append("\"blocks\":[\n");
		boolean first = true;
		for (MemoryBlock b : mem.getBlocks()) {
			AddressSet bs = new AddressSet(b.getStart(), b.getEnd());
			int nf = 0;
			long fbytes = 0;
			Map<String, Integer> tagc = new TreeMap<>();
			for (Function f : funcs) {
				if (bs.contains(f.getEntryPoint())) {
					nf++;
					for (FunctionTag t : f.getTags()) {
						tagc.merge(t.getName(), 1, Integer::sum);
					}
					if (f.getTags().isEmpty()) {
						tagc.merge("(analysis)", 1, Integer::sum);
					}
				}
				fbytes += f.getBody().intersect(bs).getNumAddresses();
			}
			long insnBytes = 0, orphanBytes = 0, dataBytes = 0, undefBytes = 0;
			if (b.isInitialized()) {
				undefBytes = listing.getUndefinedRanges(bs, true, monitor).getNumAddresses();
				InstructionIterator ii = listing.getInstructions(bs, true);
				while (ii.hasNext()) {
					Instruction ins = ii.next();
					insnBytes += ins.getLength();
					if (listing.getFunctionContaining(ins.getAddress()) == null) {
						orphanBytes += ins.getLength();
					}
				}
				dataBytes = b.getSize() - insnBytes - undefBytes;
			}
			if (!first) {
				sb.append(",\n");
			}
			first = false;
			sb.append(String.format(
				"{\"name\":%s,\"start\":\"%s\",\"end\":\"%s\",\"perms\":\"%s%s%s\"," +
					"\"initialized\":%b,\"functions\":%d,\"function_bytes\":%d," +
					"\"instruction_bytes\":%d,\"orphan_instruction_bytes\":%d," +
					"\"data_bytes\":%d,\"undefined_bytes\":%d,\"found_by\":{",
				js(b.getName()), hx(b.getStart()), hx(b.getEnd().getOffset() + 1),
				b.isRead() ? "r" : "-", b.isWrite() ? "w" : "-", b.isExecute() ? "x" : "-",
				b.isInitialized(), nf, fbytes, insnBytes, orphanBytes, dataBytes, undefBytes));
			boolean f2 = true;
			for (Map.Entry<String, Integer> e : tagc.entrySet()) {
				if (!f2) {
					sb.append(',');
				}
				f2 = false;
				sb.append(js(e.getKey())).append(':').append(e.getValue());
			}
			sb.append("}}");
		}
		sb.append("\n],\n");

		Map<String, Integer> dst = new TreeMap<>();
		List<Long> noret = new ArrayList<>();
		int thunks = 0;
		for (Function f : funcs) {
			String s = status.getOrDefault(f.getEntryPoint(), "skipped");
			if (s.startsWith("error")) {
				s = "error";
			}
			dst.merge(s, 1, Integer::sum);
			if (f.hasNoReturn()) {
				noret.add(f.getEntryPoint().getOffset());
			}
			if (f.isThunk()) {
				thunks++;
			}
		}
		sb.append("\"decomp\":{");
		first = true;
		for (Map.Entry<String, Integer> e : dst.entrySet()) {
			if (!first) {
				sb.append(',');
			}
			first = false;
			sb.append(js(e.getKey())).append(':').append(e.getValue());
		}
		sb.append("},\n");
		sb.append("\"thunks\":").append(thunks).append(",\n");
		sb.append("\"noreturn\":[").append(hexList(noret)).append("],\n");
		for (Map.Entry<String, Object> e : summary.entrySet()) {
			sb.append(js(e.getKey())).append(':').append(e.getValue()).append(",\n");
		}
		sb.append("\"timing\":{");
		first = true;
		for (Map.Entry<String, Double> e : timing.entrySet()) {
			if (!first) {
				sb.append(',');
			}
			first = false;
			sb.append(js(e.getKey())).append(':').append(String.format("%.1f", e.getValue()));
		}
		sb.append("}\n}\n");
		write(new File(outDir, "summary.json"), sb.toString());
	}

	// ------------------------------------------------------------------ decompile

	private String decompileOne(Function f, int timeoutSec, DecompileConfigurer cfg,
			boolean clearFpscr, File dir, boolean perFile, Map<Long, String> single,
			Map<Address, String> status) throws Exception {
		ProgramContext pc = currentProgram.getProgramContext();
		List<Register> regs = new ArrayList<>();
		if (clearFpscr) {
			for (String rn : new String[] { "FPSCR_PR", "FPSCR_SZ", "FPSCR_FR" }) {
				Register r = pc.getRegister(rn);
				if (r != null) {
					regs.add(r);
					for (AddressRange ar : f.getBody()) {
						pc.remove(ar.getMinAddress(), ar.getMaxAddress(), r);
					}
				}
			}
		}
		DecompInterface di = new DecompInterface();
		String st;
		try {
			cfg.configure(di);
			di.openProgram(currentProgram);
			DecompileResults res = di.decompileFunction(f, timeoutSec, monitor);
			st = emit(f, res, null, dir, perFile, single, status,
				clearFpscr ? "decompiled without the FPSCR_PR/SZ/FR=0 context (decompiler crashed with it)" : null);
		}
		catch (Exception e) {
			st = emit(f, null, "error: " + e.getMessage(), dir, perFile, single, status, null);
		}
		finally {
			di.dispose();
			for (Register r : regs) {
				for (AddressRange ar : f.getBody()) {
					pc.setValue(r, ar.getMinAddress(), ar.getMaxAddress(), java.math.BigInteger.ZERO);
				}
			}
		}
		return st;
	}

	/** Write one decompilation result (or failure stub); returns the status string. */
	private String emit(Function f, DecompileResults res, String forced, File dir, boolean perFile,
			Map<Long, String> single, Map<Address, String> status) throws IOException {
		return emit(f, res, forced, dir, perFile, single, status, null);
	}

	private String emit(Function f, DecompileResults res, String forced, File dir, boolean perFile,
			Map<Long, String> single, Map<Address, String> status, String note) throws IOException {
		String st = forced;
		String code = null;
		if (st == null) {
			try {
				if (res != null && res.decompileCompleted() && res.getDecompiledFunction() != null) {
					st = "ok";
					code = res.getDecompiledFunction().getC();
				}
				else if (res != null && res.isTimedOut()) {
					st = "timeout";
				}
				else {
					String em = res == null ? null : res.getErrorMessage();
					st = "error: " + (em == null ? "unknown" : em.trim().replace('\n', ' '));
				}
			}
			catch (Exception e) {
				st = "error: " + e.getMessage();
			}
		}
		st = st.replace('\n', ' ');
		Address ep = f.getEntryPoint();
		AddressSetView body = f.getBody();
		StringBuilder hdr = new StringBuilder();
		hdr.append("// ").append(f.getName()).append(" @ ").append(hx(ep))
				.append("  size=0x").append(Long.toHexString(body.getNumAddresses()))
				.append("  end=").append(hx(body.getMaxAddress().getOffset() + 1))
				.append("  block=").append(blockName(ep)).append('\n');
		if (!"ok".equals(st)) {
			hdr.append("// DECOMPILATION FAILED: ").append(st).append('\n');
		}
		if (note != null && "ok".equals(st)) {
			hdr.append("// NOTE: ").append(note).append('\n');
		}
		String text = hdr + (code == null ? "" : code);
		if (perFile) {
			write(new File(dir, String.format("%08X.c", ep.getOffset())), text);
		}
		if (single != null) {
			single.put(ep.getOffset(), text);
		}
		status.put(ep, st);
		return st;
	}

	private List<long[]> parseRanges(String spec) {
		List<long[]> out = new ArrayList<>();
		for (String p : spec.split("[;,]")) {
			p = p.trim();
			if (p.isEmpty()) {
				continue;
			}
			String[] se = p.split("-");
			out.add(new long[] { Long.parseUnsignedLong(se[0].replaceFirst("^0[xX]", ""), 16),
				Long.parseUnsignedLong(se[1].replaceFirst("^0[xX]", ""), 16) });
		}
		return out;
	}

	private void decompileAll(List<Function> funcs, String sel, Map<Address, String> status,
			Map<Address, Long> ms) throws Exception {
		List<long[]> ranges = "all".equals(sel) ? null : parseRanges(sel);
		List<Function> todo = new ArrayList<>();
		for (Function f : funcs) {
			if (f.isExternal()) {
				continue;
			}
			MemoryBlock blk = mem.getBlock(f.getEntryPoint());
			if (blk == null || !blk.isInitialized()) {
				status.put(f.getEntryPoint(), "skipped_uninitialized");
				continue;
			}
			long e = f.getEntryPoint().getOffset();
			boolean in = ranges == null;
			if (!in) {
				for (long[] r : ranges) {
					if (e >= r[0] && e < r[1]) {
						in = true;
						break;
					}
				}
			}
			if (in) {
				todo.add(f);
			}
		}
		String mode = args.getOrDefault("decompout", "files");
		final boolean perFile = !"single".equals(mode);
		final Map<Long, String> single =
			"files".equals(mode) ? null : new java.util.concurrent.ConcurrentSkipListMap<>();
		File dir = new File(outDir, "decomp");
		dir.mkdirs();
		File[] old = dir.listFiles((d, n) -> n.endsWith(".c"));
		if (old != null) {
			for (File o : old) {
				o.delete();
			}
		}
		int timeout = Integer.parseInt(args.getOrDefault("timeout", "30"));
		if (args.containsKey("threads")) {
			GThreadPool.getSharedThreadPool("Parallel Decompiler")
					.setMaxThreadCount(Integer.parseInt(args.get("threads")));
		}
		println("decompiling " + todo.size() + " functions, timeout " + timeout + "s");

		final Program prog = currentProgram;
		DecompileConfigurer cfg = di -> {
			DecompileOptions opts = new DecompileOptions();
			opts.grabFromProgram(prog);
			di.setOptions(opts);
			di.toggleCCode(true);
			di.toggleSyntaxTree(true);
			di.setSimplificationStyle("decompile");
		};
		final int[] done = { 0 };
		DecompilerCallback<String> cb = new DecompilerCallback<>(prog, cfg) {
			@Override
			public String process(Function f, TaskMonitor m) throws Exception {
				long t1 = System.nanoTime();
				String r;
				try {
					r = super.process(f, m);
				}
				catch (Exception e) {
					r = emit(f, null, "error: " + e.getMessage(), dir, perFile, single, status);
				}
				ms.put(f.getEntryPoint(), (System.nanoTime() - t1) / 1000000L);
				synchronized (done) {
					done[0]++;
					if (done[0] % 500 == 0) {
						println("  decompiled " + done[0] + "/" + todo.size());
					}
				}
				return r;
			}

			@Override
			public String process(DecompileResults res, TaskMonitor m) throws Exception {
				return emit(res.getFunction(), res, null, dir, perFile, single, status);
			}
		};
		cb.setTimeout(timeout);
		try {
			long t0 = System.currentTimeMillis();
			ParallelDecompiler.decompileFunctions(cb, todo, monitor);
			println("decompile wall time " + (System.currentTimeMillis() - t0) / 1000 + " s");
		}
		finally {
			cb.dispose();
		}

		// retry failures one at a time (a crashed decompiler process takes its batch down)
		List<Function> retry = new ArrayList<>();
		for (Function f : todo) {
			String st = status.get(f.getEntryPoint());
			if (st == null || !st.equals("ok")) {
				retry.add(f);
			}
		}
		if (!retry.isEmpty()) {
			println("retrying " + retry.size() + " failed decompilations sequentially");
			int fixed = 0, noFpscr = 0;
			for (Function f : retry) {
				monitor.checkCancelled();
				long t1 = System.nanoTime();
				String st = decompileOne(f, timeout * 2, cfg, false, dir, perFile, single, status);
				if (!"ok".equals(st)) {
					// some functions crash the native decompiler only when FPSCR_PR/SZ/FR are
					// known constants; try once more without that context for this body
					st = decompileOne(f, timeout * 2, cfg, true, dir, perFile, single, status);
					if ("ok".equals(st)) {
						noFpscr++;
					}
				}
				ms.put(f.getEntryPoint(), (System.nanoTime() - t1) / 1000000L);
				if ("ok".equals(st)) {
					fixed++;
				}
			}
			println("retry: " + fixed + "/" + retry.size() + " now ok (" + noFpscr +
				" only without the FPSCR context)");
			summary.put("decomp_retry_ok", fixed);
			summary.put("decomp_without_fpscr_context", noFpscr);
		}
		if (single != null) {
			StringBuilder all = new StringBuilder();
			for (String t : single.values()) {
				all.append(t).append("\n");
			}
			write(new File(outDir, "decomp_all.c"), all.toString());
		}
	}
}
