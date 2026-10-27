// SADC pre-analysis setup for raw Dreamcast (SH-4 LE) binaries.
//
// Script args (key=value, all optional):
//   regions=<spec>      memory map, comma separated items NAME:START-END:FLAGS
//                       START/END hex, END exclusive; FLAGS from r,w,x,v(olatile),
//                       u (= create a new uninitialized block, e.g. BSS).
//                       Initialized items split/rename the block(s) created by the loader.
//   entry=<hex>         external entry point (disassembled + function created)
//   entryname=<name>    label for the entry point
//   seeds=<file>[;...]  seed files. Formats per line ('#' starts a comment):
//                         8C0123AB [name]
//                         Name = 0x8C0123AB;       (sa2dc symbols.*.txt style)
//                       Seeds in executable blocks become functions (tag SADC_SEED);
//                       named seeds in non-executable blocks only get a label.
//   opt.<Analysis Option Name>=<value>   passed to setAnalysisOption()
//   fpscr=0             do not set FPSCR_PR/SZ/FR=0 register values over executable memory
//   analyze=1           run analyzeChanges() at the end (use with -process -noanalysis)
//@category SADC

import java.io.File;
import java.math.BigInteger;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;

public class SadcSetup extends GhidraScript {

	static final String SEED_TAG = "SADC_SEED";

	private final Map<String, String> args = new LinkedHashMap<>();

	@Override
	protected void run() throws Exception {
		for (String a : getScriptArgs()) {
			int i = a.indexOf('=');
			if (i > 0) {
				args.put(a.substring(0, i).trim(), a.substring(i + 1).trim());
			}
			else {
				args.put(a.trim(), "1");
			}
		}
		println("SadcSetup args: " + args);

		if (args.containsKey("regions") && !args.get("regions").isEmpty()) {
			applyRegions(args.get("regions"));
		}
		if (!"0".equals(args.get("fpscr"))) {
			setFpscrSingle();
		}
		for (Map.Entry<String, String> e : args.entrySet()) {
			if (e.getKey().startsWith("opt.")) {
				String opt = e.getKey().substring(4);
				println("analysis option: " + opt + " = " + e.getValue());
				setAnalysisOption(currentProgram, opt, e.getValue());
			}
		}

		LinkedHashMap<Address, String> seeds = new LinkedHashMap<>();
		if (args.containsKey("entry")) {
			Address ep = toAddr(Long.parseUnsignedLong(stripHex(args.get("entry")), 16));
			currentProgram.getSymbolTable().addExternalEntryPoint(ep);
			seeds.put(ep, args.get("entryname"));
		}
		if (args.containsKey("seeds") && !args.get("seeds").isEmpty()) {
			for (String f : args.get("seeds").split(";")) {
				if (!f.isBlank()) {
					readSeeds(new File(f.trim()), seeds);
				}
			}
		}
		if (!seeds.isEmpty()) {
			applySeeds(seeds);
		}
		if ("1".equals(args.get("analyze"))) {
			println("analyzeChanges...");
			analyzeChanges(currentProgram);
		}
	}

	static String stripHex(String s) {
		s = s.trim();
		if (s.startsWith("0x") || s.startsWith("0X")) {
			s = s.substring(2);
		}
		return s;
	}

	/**
	 * SHC -fpu=single code runs with FPSCR.PR=0, SZ=0, FR=0. The SuperH4 SLEIGH spec reads
	 * the pseudo registers FPSCR_PR/FPSCR_SZ/FPSCR_FR in every FPU instruction; giving them
	 * a value over all executable memory lets the decompiler drop the double-precision /
	 * pair-move alternatives (otherwise it prints in_FPSCR_PR tests everywhere).
	 */
	private void setFpscrSingle() throws Exception {
		ProgramContext pc = currentProgram.getProgramContext();
		AddressSetView ex = currentProgram.getMemory().getExecuteSet();
		int n = 0;
		for (String rn : new String[] { "FPSCR_PR", "FPSCR_SZ", "FPSCR_FR" }) {
			Register r = pc.getRegister(rn);
			if (r == null) {
				continue;
			}
			for (AddressRange ar : ex) {
				pc.setValue(r, ar.getMinAddress(), ar.getMaxAddress(), BigInteger.ZERO);
			}
			n++;
		}
		println("FPSCR_PR/SZ/FR = 0 over executable memory (" + n + " registers)");
	}

	// ---------------------------------------------------------------- memory map

	private void applyRegions(String spec) throws Exception {
		Memory mem = currentProgram.getMemory();
		for (String item : spec.split(",")) {
			item = item.trim();
			if (item.isEmpty()) {
				continue;
			}
			String[] p = item.split(":");
			if (p.length < 2) {
				throw new IllegalArgumentException("bad region item: " + item);
			}
			String name = p[0];
			String[] se = p[1].split("-");
			long s = Long.parseUnsignedLong(stripHex(se[0]), 16);
			long e = Long.parseUnsignedLong(stripHex(se[1]), 16);
			String flags = p.length > 2 ? p[2].toLowerCase() : "rw";
			Address start = toAddr(s);
			Address last = toAddr(e - 1);

			if (flags.contains("u")) {
				AddressSet want = new AddressSet(start, last);
				AddressSet missing = want.subtract(mem);
				int n = 0;
				for (AddressRange r : missing) {
					String bn = n == 0 ? name : name + "_" + n;
					MemoryBlock b = mem.createUninitializedBlock(bn, r.getMinAddress(),
						r.getLength(), false);
					setFlags(b, flags);
					println(String.format("region %-10s %s-%s %s (uninitialized, new)", bn,
						r.getMinAddress(), r.getMaxAddress(), flags));
					n++;
				}
				if (!want.subtract(missing).isEmpty()) {
					println("region " + name + ": part already mapped, left as is: " +
						want.subtract(missing));
				}
				continue;
			}

			// split existing blocks at the boundaries
			MemoryBlock b = mem.getBlock(start);
			if (b == null) {
				println("region " + name + ": start " + start + " not mapped, skipped");
				continue;
			}
			if (!b.getStart().equals(start)) {
				mem.split(b, start);
			}
			MemoryBlock lb = mem.getBlock(last);
			if (lb != null && lb.getEnd().compareTo(last) > 0) {
				mem.split(lb, last.add(1));
			}
			// rename + set flags for every block fully inside [start,last]
			int n = 0;
			for (MemoryBlock blk : mem.getBlocks()) {
				if (blk.getStart().compareTo(start) >= 0 && blk.getEnd().compareTo(last) <= 0) {
					String bn = n == 0 ? name : name + "_" + n;
					if (!blk.getName().equals(bn)) {
						blk.setName(bn);
					}
					setFlags(blk, flags);
					println(String.format("region %-10s %s-%s %s", bn, blk.getStart(),
						blk.getEnd(), flags));
					n++;
				}
			}
		}
	}

	private void setFlags(MemoryBlock b, String flags) {
		b.setRead(flags.contains("r"));
		b.setWrite(flags.contains("w"));
		b.setExecute(flags.contains("x"));
		b.setVolatile(flags.contains("v"));
	}

	// ---------------------------------------------------------------- seeds

	private static final Pattern SYM_ASSIGN =
		Pattern.compile("^\\s*([A-Za-z_.$?@][\\w.$?@]*)\\s*=\\s*(?:0[xX])?([0-9A-Fa-f]{1,8})\\s*;?");
	private static final Pattern ADDR_FIRST =
		Pattern.compile("^\\s*(?:0[xX])?([0-9A-Fa-f]{1,8})(?:\\s+([A-Za-z_.$?@][\\w.$?@]*))?");

	private void readSeeds(File f, Map<Address, String> out) throws Exception {
		if (!f.isFile()) {
			printerr("seed file not found: " + f);
			return;
		}
		int n = 0;
		for (String line : Files.readAllLines(f.toPath(), StandardCharsets.UTF_8)) {
			int c = line.indexOf('#');
			if (c >= 0) {
				line = line.substring(0, c);
			}
			if (line.isBlank()) {
				continue;
			}
			String name = null;
			long v;
			Matcher m = SYM_ASSIGN.matcher(line);
			if (m.find()) {
				name = m.group(1);
				v = Long.parseUnsignedLong(m.group(2), 16);
			}
			else {
				m = ADDR_FIRST.matcher(line);
				if (!m.find()) {
					continue;
				}
				v = Long.parseUnsignedLong(m.group(1), 16);
				name = m.group(2);
			}
			Address a = toAddr(v);
			if (!out.containsKey(a) || out.get(a) == null) {
				out.put(a, name);
			}
			n++;
		}
		println("seed file " + f + ": " + n + " entries");
	}

	private void applySeeds(Map<Address, String> seeds) throws Exception {
		Memory mem = currentProgram.getMemory();
		Listing listing = currentProgram.getListing();
		SymbolTable st = currentProgram.getSymbolTable();

		AddressSet code = new AddressSet();
		int labels = 0, skipped = 0;
		for (Map.Entry<Address, String> e : seeds.entrySet()) {
			Address a = e.getKey();
			MemoryBlock b = mem.getBlock(a);
			if (b == null || !b.isInitialized()) {
				skipped++;
				continue;
			}
			if (!b.isExecute() || (a.getOffset() & 1) != 0) {
				if (e.getValue() != null) {
					st.createLabel(a, e.getValue(), SourceType.IMPORTED);
					labels++;
				}
				else {
					skipped++;
				}
				continue;
			}
			code.add(a);
		}
		println("disassembling " + code.getNumAddresses() + " seed entries...");
		DisassembleCommand cmd = new DisassembleCommand(code, null, true);
		cmd.applyTo(currentProgram, monitor);

		int created = 0, existed = 0, failed = 0;
		for (Address a : code.getAddresses(true)) {
			monitor.checkCancelled();
			String name = seeds.get(a);
			Function f = listing.getFunctionAt(a);
			if (f == null) {
				if (listing.getInstructionAt(a) == null) {
					failed++;
					println("seed " + a + ": no instruction (conflict/offcut), skipped");
					continue;
				}
				f = createFunction(a, name);
				if (f == null) {
					failed++;
					println("seed " + a + ": createFunction failed");
					continue;
				}
				created++;
			}
			else {
				existed++;
				if (name != null && f.getSymbol().getSource() == SourceType.DEFAULT) {
					f.setName(name, SourceType.IMPORTED);
				}
			}
			f.addTag(SEED_TAG);
		}
		println(String.format(
			"seeds: %d total, %d functions created, %d already existed, %d failed, %d labels, %d skipped",
			seeds.size(), created, existed, failed, labels, skipped));
	}
}
