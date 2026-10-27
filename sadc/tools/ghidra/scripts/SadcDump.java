// Debug helper: print the listing (code units, owning function, references) for ranges.
// Args: out=<file> (optional, default: script log) then one or more START-END (hex, END exclusive)
//@category SADC

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class SadcDump extends GhidraScript {
	@Override
	protected void run() throws Exception {
		StringBuilder sb = new StringBuilder();
		String out = null;
		Listing listing = currentProgram.getListing();
		for (String a : getScriptArgs()) {
			if (a.startsWith("out=")) {
				out = a.substring(4);
				continue;
			}
			String[] se = a.split("-");
			Address s = toAddr(Long.parseUnsignedLong(se[0].replaceFirst("^0[xX]", ""), 16));
			Address e = toAddr(Long.parseUnsignedLong(se[1].replaceFirst("^0[xX]", ""), 16) - 1);
			for (CodeUnit cu : listing.getCodeUnits(new AddressSet(s, e), true)) {
				Function f = getFunctionContaining(cu.getAddress());
				Function fa = getFunctionAt(cu.getAddress());
				if (fa != null) {
					sb.append(String.format("------ FUNCTION %s  tags=%s\n", fa.getName(), fa.getTags()));
				}
				sb.append(String.format("%s  %-8s %-40s", cu.getAddress(),
					f == null ? "" : "[" + f.getEntryPoint() + "]", cu.toString()));
				if (cu instanceof Instruction ins) {
					sb.append(" flow=").append(ins.getFlowType());
					if (ins.isInDelaySlot()) {
						sb.append(" (slot)");
					}
				}
				for (Reference r : cu.getReferencesFrom()) {
					sb.append("  ->").append(r.getToAddress()).append(':').append(r.getReferenceType());
				}
				sb.append('\n');
			}
		}
		if (out != null) {
			Files.writeString(new File(out).toPath(), sb.toString(), StandardCharsets.UTF_8);
		}
		else {
			println(sb.toString());
		}
	}
}
