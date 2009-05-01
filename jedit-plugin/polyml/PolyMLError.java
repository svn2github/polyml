package polyml;

import java.util.Iterator;

public class PolyMLError {
	static char KIND_FATAL = 'E';
	static char KIND_WARNING = 'W';
	
	int kind;
	public int startPos;
	public int endPos;
	public String message;
	
	public PolyMLError(char k, int s, int e, String m){
		kind = k;
		startPos = s;
		endPos = e;
		message = m;
	}
	
	
	public PolyMLError(PolyMarkup m) throws MarkupException{
		
		Iterator<PolyMarkup> i = m.getSubs().iterator();
		String s = i.next().getContent();
		if(s.length() != 1){ throw new MarkupException("PolyMLError: bad error kind length", m); }
		char c = s.charAt(0);
		if(c == KIND_FATAL || c == KIND_WARNING) {
			kind = c;
			
			s = i.next().getContent();
			startPos = Integer.parseInt(s);
			
			s = i.next().getContent();
			endPos = Integer.parseInt(s);
			
			message = i.next().getContent();
			
		} else {
			throw new MarkupException("PolyMLError: bad error kind", m);
		}
		
	}
	
	public String stringOfError() {
		String statusString, locationString;
	
		if(kind == KIND_FATAL) {
			statusString = "Error:";
		} else if(kind == KIND_WARNING) {
			statusString = "Warning:";
		} else {
			statusString = "Unkown Status: " + kind;
		}
		
		locationString = " in: " + startPos + "-" + endPos;
		
		return statusString + locationString + "\n" + message + "\n\n";
	}
	
	public boolean isFatal(){
		return (kind == KIND_FATAL);
	}
}