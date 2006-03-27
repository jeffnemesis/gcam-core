package ModelInterface.ModelGUI2;

import java.awt.Component;
import java.awt.Graphics;
import java.awt.Rectangle;
import java.awt.event.MouseAdapter;
import java.awt.event.MouseEvent;
import javax.swing.Icon;
import javax.swing.ImageIcon;
import javax.swing.JTabbedPane;

import ModelInterface.InterfaceMain;

public class TabCloseIcon implements Icon {

	private Icon showingIcon;
	private final Icon closeIcon;
	//private final Icon mOverCloseIcon;
	private final Icon mPressCloseIcon;
	private JTabbedPane tabPane = null;
	private transient Rectangle position = null;
	
	public TabCloseIcon() {
		closeIcon = new ImageIcon( TabCloseIcon.class.getResource("icons/closeTab.PNG"));
		//mOverCloseIcon = new ImageIcon( TabCloseIcon.class.getResource("icons/mOverCloseTab.PNG"));
		mPressCloseIcon = new ImageIcon( TabCloseIcon.class.getResource("icons/mPressCloseTab.PNG"));
		showingIcon = closeIcon;
	}
	
	public void paintIcon(Component c, Graphics g, int x, int y) {
		if( tabPane == null) {
			tabPane = (JTabbedPane)c;
			tabPane.addMouseListener( new MouseAdapter() {
				@Override public void mouseReleased( MouseEvent e ) {
					// asking for isConsumed is *very* important, otherwise more than one tab might get closed!
					if ( !e.isConsumed()  &&   position.contains( e.getX(), e.getY() ) ) {
						//final int index = tabPane.getSelectedIndex();
						final int index = tabPane.indexAtLocation(e.getX(), e.getY());
						InterfaceMain.getInstance().fireProperty("Query", 
								DbViewer.getTableModelFromComponent(tabPane.getComponentAt(index)), null);
						tabPane.remove( index );
						e.consume();
					}
				}
				/*
				@Override public void mouseEntered( MouseEvent e ) {
					System.out.println("Entered Called");
					System.out.println(position);
					System.out.println("("+e.getX()+", "+e.getY()+")");
					if ( !e.isConsumed()  &&   position.contains( e.getX(), e.getY() ) ) {
						System.out.println("Entered IF");
						showingIcon = mOverCloseIcon;
						//showingIcon.paintIcon(c, g, x, y );
					}
				}
				*/
				@Override public void mouseExited( MouseEvent e ) {
					if ( !e.isConsumed() /* &&   position.contains( e.getX(), e.getY() )*/ ) {
						showingIcon = closeIcon;
						//showingIcon.paintIcon(c, g, x, y );
					}
				}
				@Override public void mousePressed( MouseEvent e ) {
					/*
					System.out.println("Pressed Called");
					System.out.println(position);
					System.out.println("("+e.getX()+", "+e.getY()+")");
					*/
					if ( !e.isConsumed()  &&   position.contains( e.getX(), e.getY() ) ) {
						//System.out.println("Pressed IF");
						showingIcon = mPressCloseIcon;
						//showingIcon.paintIcon(c, g, x, y );
					}
				}
			});
		}
		
		position = new Rectangle( x,y, getIconWidth(), getIconHeight() );
		showingIcon.paintIcon(c, g, x, y );
	}
	
	public int getIconWidth()
	{
		return showingIcon.getIconWidth();
	}
	
	public int getIconHeight()
	{
		return showingIcon.getIconHeight();
	}
}